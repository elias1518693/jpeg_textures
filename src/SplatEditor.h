#pragma once

#include "glm/gtc/matrix_access.hpp"

#include "OpenVRHelper.h"

#include "CudaVirtualMemory.h"
#include "CURuntime.h"

#include "./scene/SceneNode.h"
#include "./scene/Scene.h"
#include "./scene/SNTriangles.h"

#include "cuda.h"
#include "cuda_runtime.h"
#include "CudaModularProgram.h"

#include "OrbitControls.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "json/json.hpp"
#include "stb/stb_image_resize2.h"
#include "Runtime.h"
#include "JPEGindexer.h"
#include "Shader.h"

using glm::transpose;
using glm::vec2;
using glm::quat;
using glm::vec3;
using glm::dvec3;
using glm::mat4;
using glm::dmat4;

struct SplatEditor{
	
	inline static SplatEditor* instance;

	Scene scene;

	CudaModularProgram* prog_jpeg = nullptr;

	MouseEvents mouse_prev;

	CUdeviceptr cptr_uniforms;

	View view_left;
	View view_right;

	DeviceState deviceState;
	void* h_state_pinned = nullptr;
	CUdeviceptr cptr_state;

	vector<JPEGIndexer> textureIndexer;
	CommonLaunchArgs launchArgs;

	struct{
		bool showBoundingBoxes = false;
		bool frontToBack = true;
		bool enableOpenglRendering = false;
		bool renderCooperative = false;
		bool enableEDL = true;

		bool hideGUI = false;

		bool renderWarpwise = false;

		bool showDevStuff = false;
		bool showKernelInfos = false;
		bool showMemoryInfos = false;
		bool showTimingInfos = false;
		bool showStats = false;

		bool showInset = false;
		float dbg_factor = 1.0f;
		bool useJpegRendering = true;
		
		bool showUVs = false;
		bool showMCUs = false;
		bool showTexID = false;
		bool showCaching = false;
		bool showMipLevel = false;

		bool enableLinearInterpolation = true;
		bool enableMipMapping = true;
		bool enableTextureBlockCaching = true;
		bool freezeCache = false;
		bool rotBenchEnabled = false;
		uint32_t rotCounter = 0;

		int num_mcus = 0;
		int max_mcus = 0;
		bool show_num_mcus = true;
	} settings;

	static void setup();

	CommonLaunchArgs getCommonLaunchArgs();

	void drawGUI();
	void resetEditor();
	void inputHandling();
	Uniforms getUniforms();
	void initCudaProgram();
	void overhead();

	// GUI
	void makeMenubar();
	void makeStats();
	void makeToolbar();
	void makeDevGUI();

	// UPDATE & DRAW 
	void update();
	void render();
	void postFrame();
	void draw(Scene* scene, vector<View> views);

};