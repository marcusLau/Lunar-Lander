
// Author: Marcus Lau
// Date: 5/7/18

#include "ofApp.h"
#include "Util.h"
#include <vector>
#include <map>

//--------------------------------------------------------------
// setup scene, lighting, state and load geometry
//
void ofApp::setup() {
	ofSetFrameRate(60);
	bShaded = true;
	bWireframe = false;
	bDisplayPoints = false;
	bAltKeyDown = false;
	bCtrlKeyDown = false;
	bRoverLoaded = false;
	bTerrainSelected = true;
	bHide = true;
	bPointSelectedOctree = false;
	landed = false; // Landing flag default set to false

	// texture loading
	ofDisableArbTex();     // disable rectangular textures
	if (!ofLoadImage(particleTex, "images/dot.png")) {
		cout << "Particle Texture File: images/dot.png not found" << endl;
		ofExit();
	}
    
    background.load("images/stars.jpg");

    #ifdef TARGET_OPENGLES
        shader.load("shaders_gles/shader");
    #else
        shader.load("shaders/shader");
    #endif

	ofSetVerticalSync(true);
	ofEnableSmoothing();

	//camera setup
	camera = new Camera();
	camera->setup();

	// setup rudimentary lighting
	//
	initLightingAndMaterials();

	mars.loadModel("geo/marssurface.obj");
	mars.setScaleNormalization(false);
	marsMesh = mars.getMesh(0);

	rover.loadModel("geo/lander.obj");
	rover.setScaleNormalization(false);
	roverMesh = rover.getMesh(0);
	bRoverLoaded = true;

	roverBox = meshBounds(roverMesh);
	boundingBox = meshBounds(marsMesh);

	// compute and calculate center vector
	roverX = (roverBox.max().x() + roverBox.min().x()) / 2;
	roverY = (roverBox.max().y() + roverBox.min().y()) / 2;
	roverZ = (roverBox.max().z() + roverBox.min().z()) / 2;
	center = ofVec3f(roverX, roverY, roverZ);

	// cout << "Center point is: " << center << endl;
	rover.setPosition(roverX, roverY + 10, roverZ);

	// compute emitter location based on rover bounding box
	bottom = ofVec3f(roverX, roverBox.min().y(), roverZ);
	// cout << "Bottom point is: " << bottom << endl;

	// generate octree
	octreeHighestDepth = 0;
	generateTree(boundingBox, marsMesh, octreeMaxDepth, octree);

	gui.setup();
	gui.add(sliderOctreeDepth.setup("Octree depth", 0, 0, octreeHighestDepth));
	gui.add(gravity.setup("Gravity", 0.2, 0, 2)); // Need to connect gui slider to actual slider and update in-app

	// setup thruster emission effect
	thruster_emitter.setVelocity(ofVec3f(0, -5, 0));
	thruster_emitter.setGroupSize(100);
	thruster_emitter.setEmitterType(DiscEmitter);
	thruster_emitter.setColor(ofColor(255, 0, 0));
	thruster_emitter.setPosition(ofVec3f(0, 10, 0));
	thruster_emitter.setLifespan(0.5);
	thruster_emitter.setRate(10000000.0);
	thruster_emitter.setParticleRadius(.1);
	thruster_emitter.setMass(10);
	thruster_emitter.discradius = 0.4;
    
    soundPlayer.load("sounds/thruster.mp3");
    soundPlayer.setLoop(true);

	// "Ship" is the particle that the lander is mapped to
	ship.color = ofColor::green;
	ship.lifespan = 10000;
	ship.position.set(roverX, roverY + 10, roverZ);
	sys.add(ship);

	sys.addForce(&thruster);
	sys.addForce(&impulseForce);
	//sys.addForce(new TurbulenceForce(ofVec3f(-0.2, -0.1, -0.3), ofVec3f(0.1, 0.2, 0.5)));
	sys.addForce(new GravityForce(ofVec3f(0, -gravity, 0)));
}

// load vertex buffer in preparation for rendering
void ofApp::loadVbo() {
	if (thruster_emitter.sys->particles.size() < 1) return;

	vector<ofVec3f> sizes;
	vector<ofVec3f> points;
    vector<ofFloatColor> colors;
	for (int i = 0; i < thruster_emitter.sys->particles.size(); i++) {
		points.push_back(thruster_emitter.sys->particles[i].position);
		sizes.push_back(ofVec3f(5));
        
        ofFloatColor color = ofColor::red;
        
        float newHue = 0 + (0.098 / (1000.0)) * (ofGetElapsedTimeMillis() - thruster_emitter.sys->particles[i].birthtime);
        float newSaturation = 1.0 - (1.0 / (1000.0)) * (ofGetElapsedTimeMillis() - thruster_emitter.sys->particles[i].birthtime);
        float newBrightness = 0.5 - (0.5 / (1000.0)) * (ofGetElapsedTimeMillis() - thruster_emitter.sys->particles[i].birthtime);
        float newAlpha = 0.196 - (0.196 / (1000.0)) * (ofGetElapsedTimeMillis() - thruster_emitter.sys->particles[i].birthtime);
        
        color.setHsb(newHue, newSaturation, newBrightness, newAlpha);
        
        colors.push_back(color);
	}
	// upload the data to the vbo
	//
	int total = (int)points.size();
	vbo.clear();
	vbo.setVertexData(&points[0], total, GL_STATIC_DRAW);
	vbo.setNormalData(&sizes[0], total, GL_STATIC_DRAW);
    vbo.setColorData(&colors[0], total, GL_STATIC_DRAW);
}


//--------------------------------------------------------------
// incrementally update scene (animation)
//
void ofApp::update() {
	if (!landed) {
		GravityForce *grav = (GravityForce*)sys.forces.at(2);
		grav->set(ofVec3f(0, -gravity, 0));
		sys.update();

		thruster_emitter.update();
		thruster_emitter.setPosition(sys.particles[0].position + ofVec3f(0, 0.5, 0));

		rover.setPosition(sys.particles[0].position.x, sys.particles[0].position.y, sys.particles[0].position.z);
		// check if rover point intersects with terrain mesh
		// if list is not empty, print out first point collided
		vector<int> selectedPoint = getCollision(octree.root, sys.particles[0].position);
		// If list of collisions is not empty
		if (selectedPoint.size() != 0) {
			// Find the closest vertex
			int closestVertex = selectedPoint[0];
			ofVec3f selected = marsMesh.getVertex(closestVertex);
			if (sys.particles[0].position.y > 20) { // rover is too high
				landed = false;
			}
			else {
				landed = true;
				cout << "Collision detected at: " << selected << endl;
			}
			sys.particles[0].forces = ofVec3f(0, 0, 0);
		}
	}
	camera->spacecraft = rover.getPosition();
}

//--------------------------------------------------------------
void ofApp::draw() {
    background.draw(0, 0, ofGetWindowWidth(), ofGetWindowHeight());
	//    cout << ofGetFrameRate() << endl;

	ofEnableDepthTest();
	loadVbo();

	//start camera
	camera->camera_begin();
	

	// draw particle system
	//
	//sys.draw();

	ofPushMatrix();
	if (bWireframe) {                    // wireframe mode  (include axis)
		ofDisableLighting();
		ofSetColor(ofColor::slateGray);
		mars.drawWireframe();
		if (bRoverLoaded) {
			rover.drawWireframe();
			// ofSetColor(ofColor::green);
			// ofDrawSphere(center, 0.05);
			//if (!bTerrainSelected);drawAxis(rover.getPosition());
		}
		//if (bTerrainSelected); drawAxis(ofVec3f(0, 0, 0));
	}
	else {
		ofEnableLighting();              // shaded mode
		mars.drawFaces();

		if (bRoverLoaded) {
			rover.drawFaces();
			//if (!bTerrainSelected); drawAxis(rover.getPosition());

			// this makes everything look glowy :)
			//
			ofEnableBlendMode(OF_BLENDMODE_ADD);
			ofEnablePointSprites();
			glDepthMask(GL_FALSE);
			//draw emission
			shader.begin();
			particleTex.bind();
			vbo.draw(GL_POINTS, 0, (int) thruster_emitter.sys->particles.size());
			particleTex.unbind();
			//thruster_emitter.draw();
			shader.end();

			ofDisablePointSprites();
			ofDisableBlendMode();
			ofEnableAlphaBlending();
			glDepthMask(GL_TRUE);
		}
		//if (bTerrainSelected); drawAxis(ofVec3f(0, 0, 0));
	}

	if (bDisplayPoints) {                // display points as an option
		glPointSize(3);
		ofSetColor(ofColor::green);
		mars.drawVertices();
	}

	// highlight selected point (draw sphere around selected point)
	//
	if (bPointSelected && !bPointSelectedOctree) {
		ofSetColor(ofColor::blue);
		ofDrawSphere(selectedPoint, .1);
	}

	ofNoFill();
	//ofSetColor(ofColor::red);
	//drawBox(boundingBox);
	//drawBox(roverBox);

	// draw octree
	//if (bPointSelectedOctree) {
	//	drawOctree(octree.root, true);
	//}
	//else {
	//	drawOctree(octree.root);
	//}

	//close camera
	ofPopMatrix();
	camera->camera_end();
	ofDisableDepthTest();

	if (!bHide) {
		gui.draw();
	}

	string AGL;
	AGL += "AGL: " + std::to_string(displayAGL());
	ofFill();
	ofSetColor(255, 255, 255, 255);
	ofDrawBitmapString(AGL, 10, 85);
}

// Draw an XYZ axis in RGB at world (0,0,0) for reference.
//
void ofApp::drawAxis(ofVec3f location) {

	ofPushMatrix();
	ofTranslate(location);

	ofSetLineWidth(1.0);

	// X Axis
	ofSetColor(ofColor(255, 0, 0));
	ofDrawLine(ofPoint(0, 0, 0), ofPoint(1, 0, 0));

	// Y Axis
	ofSetColor(ofColor(0, 255, 0));
	ofDrawLine(ofPoint(0, 0, 0), ofPoint(0, 1, 0));

	// Z Axis
	ofSetColor(ofColor(0, 0, 255));
	ofDrawLine(ofPoint(0, 0, 0), ofPoint(0, 0, 1));

	ofPopMatrix();
}

// Draws the octree
void ofApp::drawOctree(const Box &node, const bool onlySelectedVertexTree) {
	if (sliderOctreeDepth == 0) return;

	if ((!onlySelectedVertexTree && sliderOctreeDepth >= node.level) ||
		(onlySelectedVertexTree && node.containsSelectedVertex)) {
		switch (node.level % 9) {
		case 0:
			ofSetColor(ofColor::white);
			break;
		case 1:
			ofSetColor(ofColor::red);
			break;
		case 2:
			ofSetColor(ofColor::blue);
			break;
		case 3:
			ofSetColor(ofColor::green);
			break;
		case 4:
			ofSetColor(ofColor::orange);
			break;
		case 5:
			ofSetColor(ofColor::violet);
			break;
		case 6:
			ofSetColor(ofColor::turquoise);
			break;
		case 7:
			ofSetColor(ofColor::fuchsia);
			break;
		case 8:
			ofSetColor(ofColor::salmon);
			break;
		}

		drawBox(node);

		for (const Box &b : node.children) {
			drawOctree(b, onlySelectedVertexTree);
		}
	}
}

void ofApp::keyPressed(int key) {

	switch (key) {
	case '1':
		camera->camera_number = 0;
		break;
	case '2':
		camera->camera_number = 1;
		break;
	case '3':
		camera->camera_number = 2;
		break;
	case '4':
		camera->camera_number = 3;
		if (camera->cam.getMouseInputEnabled()) camera->cam.disableMouseInput();
		else camera->cam.enableMouseInput();
		break;

	case 'C':
	case 'c':
		if (cam.getMouseInputEnabled()) cam.disableMouseInput();
		else cam.enableMouseInput();
		break;
	case 'F':
	case 'f':
		ofToggleFullscreen();
		break;
	case OF_KEY_DOWN:
		if (!landed) {
			thruster.add(ofVec3f(0, 0, 0.5)); // was 0, 0.5, 0
		}
		break;
	case ' ':
		if (!landed) {
			if (!thruster_emitter.started) thruster_emitter.start();
            if (!soundPlayer.isPlaying()) soundPlayer.play();
			thruster.add(ofVec3f(0, .5, 0));
		}

		break;
	case OF_KEY_UP:
		if (!landed) {
			thruster.add(ofVec3f(0, 0, -0.5));
		}
		break;
	case OF_KEY_LEFT:
		if (!landed) {
			thruster.add(ofVec3f(-.5, 0, 0));
		}

		break;
	case OF_KEY_RIGHT:
		if (!landed) {
            soundPlayer.play();
			thruster.add(ofVec3f(.5, 0, 0));
        }
		break;
	case 'H':
	case 'h':
		bHide = !bHide; // toggle gui
		break;
	case 'r':
		//cam.reset();
		camera->setTarget = true;
		break;
	case 's':
		savePicture();
		break;
	case 'S':
		toggleShadedMode();
		break;
	case 't':
		setCameraTarget();
		break;
	case 'u':
		break;
	case 'v':
		togglePointsDisplay();
		break;
	case 'V':
		togglePointSelectedOctree();
		break;
	case 'w':
		toggleWireframeMode();
		break;
	case 'G':
	case 'g':
		if (show_gui) show_gui = false;
		else show_gui = true;
		break;
	case OF_KEY_ALT:
		cam.enableMouseInput();
		bAltKeyDown = true;
		break;
	case OF_KEY_CONTROL:
		bCtrlKeyDown = true;
		break;
	case OF_KEY_SHIFT:
		break;
	case OF_KEY_DEL:
		break;
	default:
		break;
	}
}

void ofApp::toggleShadedMode() {
	bShaded = !bShaded;
}

void ofApp::toggleWireframeMode() {
	bWireframe = !bWireframe;
}

void ofApp::toggleSelectTerrain() {
	bTerrainSelected = !bTerrainSelected;
}

void ofApp::togglePointsDisplay() {
	bDisplayPoints = !bDisplayPoints;
}

void ofApp::togglePointSelectedOctree() {
	bPointSelectedOctree = !bPointSelectedOctree;
}

void ofApp::keyReleased(int key) {
	switch (key) {
	case ' ':
		thruster_emitter.stop();
        soundPlayer.stop();
	case OF_KEY_RIGHT:
	case OF_KEY_LEFT:
	case OF_KEY_UP:
	case OF_KEY_DOWN:
		thruster.set(ofVec3f(0, 0, 0));
		break;
	case OF_KEY_ALT:
		cam.disableMouseInput();
		bAltKeyDown = false;
		break;
	case OF_KEY_CONTROL:
		bCtrlKeyDown = false;
		break;
	case OF_KEY_SHIFT:
		break;
	default:
		break;
	}
}

void ofApp::mouseMoved(int x, int y) {
}

// AGL displayed on top right
float ofApp::displayAGL() {
	float result = 0;
	ofVec3f selected = ofVec3f(0, 0, 0);

	Ray ray = Ray(Vector3(sys.particles[0].position.x, sys.particles[0].position.y, sys.particles[0].position.z),
		Vector3(0, -1, 0));
	vector<int> selectedVertices = getIntersectingVertices(octree.root, ray);

	if (selectedVertices.size() != 0) {
		// Find the closest vertex
		int closestVertex = selectedVertices[0];
		for (int i : selectedVertices) {
			if (marsMesh.getVertex(i).y > marsMesh.getVertex(closestVertex).y
				&& marsMesh.getVertex(closestVertex).y > 20)
				closestVertex = i;

		}
		selected = marsMesh.getVertex(closestVertex);
	}

	result = sys.particles[0].position.y - selected.y;
	return result;
}

void ofApp::mousePressed(int x, int y, int button) {

}

// Checks which vetices intersect with ray; returns vertex indices of intersection,
// empty vector if no leaf found with vertices.
vector<int> ofApp::getIntersectingVertices(Box &box, const Ray &ray) {
	if (box.intersect(ray, 0, 100)) {
		// if only one vertex in leaf, we've found the lowest depth
		if (box.vertexIndices.size() == 1) return { box.vertexIndices[0] };

		// accumulate growing vector of intersected vertices
		vector<int> selectedVertices;
		for (Box &child : box.children) {
			vector<int> vertices = getIntersectingVertices(child, ray);
			for (int vertex : vertices) {
				selectedVertices.push_back(vertex);
			}
		}
		if (selectedVertices.size() == 0) return { };

		box.containsSelectedVertex = true;

		return selectedVertices;
	}
	else {
		box.containsSelectedVertex = false;
		return { };
	}
}

// Checks if rover point intersects with bounding box
// return a list of points that the rover point collides with
vector<int> ofApp::getCollision(Box &box, const ofPoint &point) {
	if (box.contains(point)) {
		// if only one vertex in leaf, we've found the lowest depth
		if (box.vertexIndices.size() == 1) return { box.vertexIndices[0] };
		// accumulate growing vector of intersected vertices
		vector<int> selectedVertices;
		for (Box &child : box.children) {
			vector<int> vertices = getCollision(child, point);
			for (int vertex : vertices) {
				selectedVertices.push_back(vertex);
			}
		}
		if (selectedVertices.size() == 0) return { };
		box.containsSelectedVertex = true;
		return selectedVertices;
	}
	else {
		box.containsSelectedVertex = false;
		return { };
	}
}


//draw a box from a "Box" class
void ofApp::drawBox(const Box &box) {
	Vector3 min = box.parameters[0];
	Vector3 max = box.parameters[1];
	Vector3 size = max - min;
	Vector3 center = size / 2 + min;
	ofVec3f p = ofVec3f(center.x(), center.y(), center.z());
	float w = size.x();
	float h = size.y();
	float d = size.z();
	ofDrawBox(p, w, h, d);
}

// return a Mesh Bounding Box for the entire Mesh
Box ofApp::meshBounds(const ofMesh & mesh) {
	int n = mesh.getNumVertices();
	ofVec3f v = mesh.getVertex(0);
	ofVec3f max = v;
	ofVec3f min = v;
	for (int i = 1; i < n; i++) {
		ofVec3f v = mesh.getVertex(i);

		if (v.x > max.x) max.x = v.x;
		else if (v.x < min.x) min.x = v.x;

		if (v.y > max.y) max.y = v.y;
		else if (v.y < min.y) min.y = v.y;

		if (v.z > max.z) max.z = v.z;
		else if (v.z < min.z) min.z = v.z;
	}
	return Box(Vector3(min.x, min.y, min.z), Vector3(max.x, max.y, max.z));
}

//  Subdivide a Box into eight(8) equal size boxes, return them in boxList;
void ofApp::subDivideBox8(const Box &box, vector<Box> & boxList) {
	Vector3 min = box.parameters[0];
	Vector3 max = box.parameters[1];
	Vector3 size = max - min;
	Vector3 center = size / 2 + min;
	float xdist = (max.x() - min.x()) / 2;
	float ydist = (max.y() - min.y()) / 2;
	float zdist = (max.z() - min.z()) / 2;
	Vector3 h = Vector3(0, ydist, 0);

	//  generate ground floor
	Box b[8];
	b[0] = Box(min, center);
	b[1] = Box(b[0].min() + Vector3(xdist, 0, 0), b[0].max() + Vector3(xdist, 0, 0));
	b[2] = Box(b[1].min() + Vector3(0, 0, zdist), b[1].max() + Vector3(0, 0, zdist));
	b[3] = Box(b[2].min() + Vector3(-xdist, 0, 0), b[2].max() + Vector3(-xdist, 0, 0));

	boxList.clear();
	for (int i = 0; i < 4; i++)
		boxList.push_back(b[i]);

	// generate second story
	for (int i = 4; i < 8; i++) {
		b[i] = Box(b[i - 4].min() + h, b[i - 4].max() + h);
		boxList.push_back(b[i]);
	}
}

// Recursive function for ofApp:generateTree(), generates an octree
void ofApp::generateTreeNodes(Box &node, const map<int, ofVec3f> &vertexList, int currentDepth, int maxDepth) {
	// Set node level
	node.level = currentDepth;
	if (currentDepth > octreeHighestDepth)
		octreeHighestDepth = currentDepth;

	// divide current node into 8 leaves
	subDivideBox8(node, node.children);

	for (int i = 0; i < node.children.size(); i++) {
		node.children[i].level = currentDepth + 1;

		map<int, ofVec3f> innerVertices;
		for (auto &pair : vertexList) {
			if (node.children[i].contains(pair.second)) {
				// if current leaf contains a vertex, add it to vertex index list
				node.children[i].vertexIndices.push_back(pair.first);
				// also add vertex to inner vector from which leaves will search through
				innerVertices.insert(pair);
			}
		}
		// delete any leaves with no vertices
		if (innerVertices.size() == 0) {
			node.children.erase(node.children.begin() + i);
			i--;
		}
		// recursively generate more leaves if more than one inner vertex
		else if (currentDepth < maxDepth && innerVertices.size() > 1)
			generateTreeNodes(node.children[i], innerVertices, node.children[i].level, maxDepth);
	}
}

// Given a bounding box & a mesh, generate an octree of a specified depth
void ofApp::generateTree(const Box &boundingBox, const ofMesh &mesh, int maxDepth, Octree &octree) {
	// Initialize list of vertices and corresponding indexes
	map<int, ofVec3f> vertexList;
	vector<ofVec3f> vertices = mesh.getVertices();
	for (int i = 0; i < vertices.size(); i++) {
		vertexList.emplace(i, vertices[i]);
	}

	octree.root = boundingBox;

	generateTreeNodes(octree.root, vertexList, 0, maxDepth);
}

void ofApp::mouseDragged(int x, int y, int button) {
}

void ofApp::mouseReleased(int x, int y, int button) {
}


//  Select Target Point on Terrain by comparing distance of mouse to
//  vertice points projected onto screenspace.
//  if a point is selected, return true, else return false;
bool ofApp::doPointSelection() {

	ofMesh mesh = mars.getMesh(0);
	int n = mesh.getNumVertices();
	float nearestDistance = 0;
	int nearestIndex = 0;

	bPointSelected = false;

	ofVec2f mouse(mouseX, mouseY);
	vector<ofVec3f> selection;

	// We check through the mesh vertices to see which ones
	// are "close" to the mouse point in screen space.  If we find
	// points that are close, we store them in a vector (dynamic array)
	for (int i = 0; i < n; i++) {
		ofVec3f vert = mesh.getVertex(i);
		ofVec3f posScreen = cam.worldToScreen(vert);
		float distance = posScreen.distance(mouse);
		if (distance < selectionRange) {
			selection.push_back(vert);
			bPointSelected = true;
		}
	}

	//  if we found selected points, we need to determine which
	//  one is closest to the eye (camera). That one is our selected target.
	if (bPointSelected) {
		float distance = 0;
		for (int i = 0; i < selection.size(); i++) {
			ofVec3f point = cam.worldToCamera(selection[i]);

			// In camera space, the camera is at (0,0,0), so distance from
			// the camera is simply the length of the point vector
			float curDist = point.length();

			if (i == 0 || curDist < distance) {
				distance = curDist;
				selectedPoint = selection[i];
			}
		}
	}
	return bPointSelected;
}

// Set the camera to use the selected point as it's new target
void ofApp::setCameraTarget() {
}

void ofApp::mouseEntered(int x, int y) {
}

void ofApp::mouseExited(int x, int y) {
}

void ofApp::windowResized(int w, int h) {
}

void ofApp::gotMessage(ofMessage msg) {
}

// setup basic ambient lighting in GL  (for now, enable just 1 light)
void ofApp::initLightingAndMaterials() {

	static float ambient[] =
	{ .5f, .5f, .5, 1.0f };
	static float diffuse[] =
	{ 1.0f, 1.0f, 1.0f, 1.0f };

	static float position[] =
	{ 5.0, 5.0, 5.0, 0.0 };

	static float lmodel_ambient[] =
	{ 1.0f, 1.0f, 1.0f, 1.0f };

	static float lmodel_twoside[] =
	{ GL_TRUE };


	glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
	glLightfv(GL_LIGHT0, GL_POSITION, position);

	glLightfv(GL_LIGHT1, GL_AMBIENT, ambient);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);
	glLightfv(GL_LIGHT1, GL_POSITION, position);


	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);
	glLightModelfv(GL_LIGHT_MODEL_TWO_SIDE, lmodel_twoside);

	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	//    glEnable(GL_LIGHT1);
	glShadeModel(GL_SMOOTH);
}

void ofApp::savePicture() {
	ofImage picture;
	picture.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
	picture.save("screenshot.png");
	cout << "picture saved" << endl;
}

// support drag-and-drop of model (.obj) file loading.  when
// model is dropped in viewport, place origin under cursor
void ofApp::dragEvent(ofDragInfo dragInfo) {

	ofVec3f point;
	mouseIntersectPlane(ofVec3f(0, 0, 0), cam.getZAxis(), point);

	if (rover.loadModel(dragInfo.files[0])) {
		rover.setScaleNormalization(false);
		rover.setScale(.005, .005, .005);
		rover.setPosition(point.x, point.y, point.z);
		bRoverLoaded = true;
	}
	else cout << "Error: Can't load model" << dragInfo.files[0] << endl;
}

bool ofApp::mouseIntersectPlane(ofVec3f planePoint, ofVec3f planeNorm, ofVec3f &point) {
	ofVec2f mouse(mouseX, mouseY);
	ofVec3f rayPoint = cam.screenToWorld(mouse);
	ofVec3f rayDir = rayPoint - cam.getPosition();
	rayDir.normalize();
	return (rayIntersectPlane(rayPoint, rayDir, planePoint, planeNorm, point));
}

