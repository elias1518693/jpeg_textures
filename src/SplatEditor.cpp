
#include "SplatEditor.h"
#include "cudaGL.h"

void SplatEditor::setup(){
	SplatEditor::instance = new SplatEditor();
	SplatEditor* editor = SplatEditor::instance;

	editor->initCudaProgram();

	editor->view_left.framebuffer = Framebuffer::create("view_left_fbo");
	editor->view_right.framebuffer = Framebuffer::create("view_rigth_fbo");
}

void SplatEditor::resetEditor(){
	scene.world->children.clear();
}


Uniforms SplatEditor::getUniforms(){
	Uniforms uniforms;
	uniforms.time            = now();
	uniforms.frameCount      = GLRenderer::frameCount;
	//uniforms.measure         = Runtime::measureTimings;
	uniforms.fragmentCounter = 0;

	uniforms.inset.show      = settings.showInset;
	uniforms.inset.start     = {16 * 60, 16 * 50};
	uniforms.inset.size      = {16, 16};

	uniforms.showUVs         = settings.showUVs;
	uniforms.showMCUs        = settings.showMCUs;
	uniforms.showTexID       = settings.showTexID;
	uniforms.showCaching     = settings.showCaching;
	uniforms.showMipLevel    = settings.showMipLevel;

	uniforms.enableLinearInterpolation = settings.enableLinearInterpolation;
	uniforms.enableMipMapping = settings.enableMipMapping;
	uniforms.freezeCache     = settings.freezeCache;

	glm::mat4 world(1.0f);
	glm::mat4 view           = GLRenderer::camera->view;
	glm::mat4 camWorld       = GLRenderer::camera->world;
	glm::mat4 proj           = GLRenderer::camera->proj;

	uniforms.world        = world;
	uniforms.camWorld     = camWorld;

	return uniforms;
}

CommonLaunchArgs SplatEditor::getCommonLaunchArgs(){

	KeyEvents keyEvents;
	keyEvents.numEvents = 0;
	for(int64_t i = 0; i < Runtime::frame_keys.size(); i++){
		KeyEvents::KeyEvent event;
		event.key = Runtime::frame_keys[i];
		event.action = Runtime::frame_actions[i];
		event.mods = Runtime::frame_mods[i];

		keyEvents.events[i] = event;

		keyEvents.numEvents++;
	}

	CommonLaunchArgs launchArgs;
	launchArgs.uniforms       = getUniforms();
	launchArgs.state          = (DeviceState*)cptr_state;
	launchArgs.keyEvents      = keyEvents;
	
	return launchArgs;
};

void SplatEditor::initCudaProgram(){

	cptr_uniforms = CURuntime::alloc("uniforms", sizeof(Uniforms));

	cuMemAllocHost((void**)&h_state_pinned , sizeof(DeviceState));
	cptr_state = CURuntime::alloc("device state", sizeof(DeviceState));
	cuMemsetD8(cptr_state, 0, sizeof(DeviceState));

	int tileSize = 16;
	int MAX_WIDTH = 4000;
	int MAX_HEIGHT = 4000;
	int MAX_TILES_X = MAX_WIDTH / tileSize;
	int MAX_TILES_Y = MAX_HEIGHT / tileSize;
	
	double t_start = now();
	
	CUcontext context;
	cuCtxGetCurrent(&context);

	prog_jpeg = new CudaModularProgram({"./src/kernels/jpeg.cu",});
	
	double seconds = now() - t_start;
	println("initialized cuda programs in {:.3f} seconds", seconds);
}

void SplatEditor::inputHandling(){
	
	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& scene = editor->scene;
	auto& launchArgs = editor->launchArgs;

	bool consumed = false;

	RenderTarget target;
	target.width = GLRenderer::width;
	target.height = GLRenderer::height;
	target.indexbuffer = nullptr;
	target.view = mat4(GLRenderer::camera->view); // * scene.transform;
	target.proj = GLRenderer::camera->proj;

	bool isCtrlDown        = Runtime::keyStates[341] != 0;
	bool isAltDown         = Runtime::keyStates[342] != 0;
	bool isShiftDown       = Runtime::keyStates[340] != 0;
	bool isLeftClicked     = Runtime::mouseEvents.button == 0 && Runtime::mouseEvents.action == 1;
	static bool isLeftDown = false;
	bool isRightClicked    = false; // right click event: press and release without move

	static struct {
		vec2 startPos;
		bool isRightDown = false;
		bool hasMoved = false;
	} rightDownState;

	if(!rightDownState.isRightDown && Runtime::mouseEvents.isRightDown){
		// right mouse just pressed
		rightDownState.startPos = {Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y};
		rightDownState.hasMoved = false;
		rightDownState.isRightDown = true;
	}else if(rightDownState.isRightDown && Runtime::mouseEvents.isRightDown){
		// right mouse still pressed
		if(rightDownState.startPos.x != Runtime::mouseEvents.pos_x || rightDownState.startPos.y != Runtime::mouseEvents.pos_y){
			rightDownState.hasMoved = true;
		}
	}else if(rightDownState.isRightDown && !Runtime::mouseEvents.isRightDown){
		// right mouse just released
		rightDownState.isRightDown = false;

		isRightClicked = rightDownState.hasMoved == false;
	}

	if(Runtime::mouseEvents.isLeftDownEvent()) isLeftDown = true;
	if(Runtime::mouseEvents.isLeftUpEvent()) isLeftDown = false;

	Runtime::controls->onMouseMove(Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y);
	Runtime::controls->onMouseScroll(Runtime::mouseEvents.wheel_x, Runtime::mouseEvents.wheel_y);
	Runtime::controls->update();

	GLRenderer::camera->view = inverse(Runtime::controls->world);
	GLRenderer::camera->world = Runtime::controls->world;
}

void SplatEditor::drawGUI() {

	if(!settings.hideGUI){
		makeMenubar();
		makeToolbar();
		makeDevGUI();
		makeStats();
	}else{
		ImVec2 kernelWindowSize = {70, 25};
		ImGui::SetNextWindowPos({GLRenderer::width - kernelWindowSize.x, -8});
		ImGui::SetNextWindowSize(kernelWindowSize);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoScrollWithMouse
			| ImGuiWindowFlags_NoCollapse
			// | ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoBackground
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoDecoration;
		static bool open;

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));

		if(ImGui::Begin("ShowGuiWindow", &open, flags)){
			if(ImGui::Button("Show GUI")){
				settings.hideGUI = !settings.hideGUI;
			}
		}
		ImGui::End();
		
		ImGui::PopStyleColor(2);

	}
}

void SplatEditor::update(){

	Runtime::timings.newFrame();
	
	string strfps = format("Splat Editor | FPS: {}", int(GLRenderer::fps));
	glfwSetWindowTitle(GLRenderer::window, strfps.c_str());

	// For the rotation benchmark: Rotate by 6 degree every frame.
	if(settings.rotBenchEnabled){
		OrbitControls* controls = Runtime::controls;

		// When we first start the rotation benchmark, remember the direction.
		static dvec3 originalDir = controls->getDirection();
		static dvec3 originalPos = controls->getPosition();
		static float originalYaw = controls->yaw;
		
		double angleDeg = (6 * settings.rotCounter) % 360;
		double angleRad = 3.1415 * angleDeg / 180.0;
		dmat4 rot = glm::rotate(angleRad, dvec3{0.0, 0.0, 1.0});

		dvec3 newDir = rot * dvec4{originalDir.x, originalDir.y, originalDir.z, 0.0};
		dvec3 newTarget = originalPos + newDir * controls->radius;

		controls->target = newTarget;
		controls->yaw = originalYaw + angleRad;

		settings.rotCounter++;
	}

	if(OpenVRHelper::instance()->isActive()){
		auto ovr = OpenVRHelper::instance();

		ovr->updatePose();
		ovr->processEvents();
	}

	scene.updateTransformations();
	Runtime::debugValues.clear();
	Runtime::debugValueList.clear();

	if(GLRenderer::width * GLRenderer::height == 0){
		return;
	}

	Timer::enabled = Runtime::measureTimings;

	launchArgs = getCommonLaunchArgs();

	scene.updateTransformations();
	inputHandling();
	scene.updateTransformations();
};

void SplatEditor::postFrame(){
	// some special benchmarking stuff

	auto editor = SplatEditor::instance;

	if(editor->settings.rotBenchEnabled){

		// For the rotation benchmark, we capture 60 viewpoint deltas 100 times.
		// - We rotate 6° per frame, for a full rotation in 60 frames
		// - We do 100 full rotations
		// - Whenever we look in viewpoint X again, we capture the time and store it in that viewpoints list

		// But why?
		// - We are interested in the most expensive viewpoint change where our method takes the longest
		// - But simply computing max is bad since frame times fluctuate strongly, which is why we'd usually take median
		// - So instead, we compute the max-of-medians.
		//     - First, for each viewpoint we compute the median of all its timings
		//     - Then, we compute the max of the medians of the viewpoints.
		constexpr int NUM_REPETITIONS = 100;
		static struct {
			float entries[NUM_REPETITIONS];
			uint32_t entryIndex = 0;
		} viewpoints[60];
		

		if(Runtime::timings.entries.contains("<jpeg pipeline>")){
			int viewpointIndex = editor->settings.rotCounter % 60;

			// fetch duration of this frame's <jpeg pipeline> measure
			int entryPos = Runtime::timings.counter % Runtime::timings.historySize;
			float duration = Runtime::timings.entries["<jpeg pipeline>"][entryPos];

			auto& viewpoint = viewpoints[viewpointIndex];
			viewpoint.entries[viewpoint.entryIndex] = duration;
			viewpoint.entryIndex = (viewpoint.entryIndex + 1) % NUM_REPETITIONS;

			bool finishedRound = (viewpointIndex == 59 && viewpoint.entryIndex == 0);

			if(finishedRound){
				// evaluate and report, then restart.
				float max = 0.0f;
				for(int i = 0; i < 60; i++){

					std::sort(viewpoints[i].entries, viewpoints[i].entries + NUM_REPETITIONS);
					float median = viewpoints[i].entries[50];
					max = std::max(max, median);
				}

				println("max-of-medians: {:.3f} ms", max);
				// println("=== REPORT END ===");
			}
		}

	}
}

void SplatEditor::overhead(){

	// Storing the first DC of each component as DC_BITS raw bits, plus the MCU
	// index, is what random access into the JPEG costs us.

	double duTotals[jpegindex::MAX_DATA_UNITS] = {0.0};
	double totalMCUs = 0.0;
	int texelsPerMCU = 0;
	vector<JpegTexture::DataUnit> layout;

	println("==================================");

	int numTextures = 0;
	int64_t numTexels = 0;
	for(int i = 0; i < jpegTextures.size(); i++){
		auto& jpeg = jpegTextures[i];

		if(jpeg.mipMapLevel != 0) continue;
		if(!jpeg.ok()) continue;

		layout = jpeg.dataUnits;
		texelsPerMCU = jpeg.mcuWidth * jpeg.mcuHeight;

		println("# Texture {}, {} x {}, {}, {} MCUs of {} x {} texels",
			i / 8, jpeg.width, jpeg.height, jpeg.samplingName(),
			jpeg.numMcus, jpeg.mcuWidth, jpeg.mcuHeight);

		println("    DC/DU data");
		for(int du = 0; du < jpeg.dataUnitsPerMcu; du++){
			double average = double(jpeg.dcBitsPerDataUnit[du]) / double(jpeg.dcCountPerDataUnit[du]);

			if(jpeg.dataUnits[du].absoluteDC){
				println("    DU[{}] (component {}): {:8}; mcus: {:8}; avg: {:4.1f} bit per DU's DC; overhead: {:4.1f} bit per DC",
					du, jpeg.dataUnits[du].component, jpeg.dcBitsPerDataUnit[du], jpeg.dcCountPerDataUnit[du],
					average, double(jpegindex::DC_BITS) - average);
			}else{
				println("    DU[{}] (component {}): {:8}; mcus: {:8}; avg: {:4.1f} bit per DU's DC; overhead: none",
					du, jpeg.dataUnits[du].component, jpeg.dcBitsPerDataUnit[du], jpeg.dcCountPerDataUnit[du], average);
			}

			duTotals[du] += double(jpeg.dcBitsPerDataUnit[du]);
		}

		totalMCUs += double(jpeg.numMcus);

		if(jpeg.numMcus != jpeg.dcCountPerDataUnit[0]){
			println("WARN: numMcus != jpeg.dcCountPerDataUnit[0]");
		}
		numTextures++;
		numTexels += int64_t(jpeg.width) * int64_t(jpeg.height);
	}

	if(numTextures == 0){
		println("no textures to report on");
		println("==================================");
		return;
	}

	println("Number of textures: {}", numTextures);
	println("Number of Texels: {} M", numTexels / 1'000'000ll);

	// bits the DCs we store as absolute values took in the original JPEG
	double originalDCBits = 0.0;
	int numAbsoluteDCs = 0;

	println("Average bit size over all MCUs: ");
	for(int du = 0; du < layout.size(); du++){
		double average = duTotals[du] / totalMCUs;

		if(layout[du].absoluteDC){
			println("    DU[{}]: {:4.3f} bit -> {:4.3f} bit per MCU overhead", du, average, double(jpegindex::DC_BITS) - average);
			originalDCBits += average;
			numAbsoluteDCs++;
		}else{
			println("    DU[{}]: {:4.3f} bit", du, average);
		}
	}

	double ours = double(jpegindex::DC_BITS) * numAbsoluteDCs;
	double perTexelOverhead_DCs = (ours - originalDCBits) / double(texelsPerMCU);
	double indexBitsPerMCU = (32.0 + 16.0 * (jpegindex::MCU_INDEX_GROUP - 1)) / double(jpegindex::MCU_INDEX_GROUP);
	double perTexelOverhead_indexing = indexBitsPerMCU / double(texelsPerMCU);

	println("Overhead per Texel for absolute DCs       {:4.3f} bit", perTexelOverhead_DCs);
	println("Overhead per Texel for indexing table     {:4.3f} bit", perTexelOverhead_indexing);
	println("Overhead per Texel                        {:4.3f} bit", perTexelOverhead_DCs + perTexelOverhead_indexing);

	println("==================================");
}


#include "SplatEditor_render.h"
#include "gui/menubar.h"
#include "gui/toolbar.h"
#include "gui/dev.h"
#include "gui/stats.h"