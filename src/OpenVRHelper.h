
#pragma once

#include <string>
#include <vector>
#include <memory>

using namespace std;

#include "openvr.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/transform.hpp>

#include "unsuck.hpp"

using std::string;
using std::vector;
using glm::dmat4;
using std::shared_ptr;

using namespace vr;

struct Pose {
	bool valid;
	dmat4 transform;
};

struct Controller{
	bool valid;
	dmat4 transform;
	VRControllerState_t state;

	struct {
		bool valid;
		dmat4 transform;
		VRControllerState_t state;
	} previous;

	bool isTriggerPressed(){
		return state.ulButtonPressed == (1llu << k_EButton_SteamVR_Trigger) != 0;
	}

	bool wasTriggerPressed(){
		return previous.state.ulButtonPressed == (1llu << k_EButton_SteamVR_Trigger) != 0;
	}

	bool triggered(){
		return isTriggerPressed() && !wasTriggerPressed();
	}

	bool untriggered(){
		return !isTriggerPressed() && wasTriggerPressed();
	}

	bool isBPressed(){
		return state.ulButtonPressed & 2;
	}

	bool wasBPressed(){
		return previous.state.ulButtonPressed & 2;
	}

	bool isAPressed(){
		return state.ulButtonPressed & 4;
	}
	
	bool wasAPressed(){
		return previous.state.ulButtonPressed & 4;
	}
};

struct UV {
	float u;
	float v;
};

struct DistortionMap {
	vector<UV> red;
	vector<UV> green;
	vector<UV> blue;
};

struct Keyframe{
	double timestamp;
	dmat4 poseHMD;
	int width;
	int height;

	Pose controllerPoseLeft;
	VRControllerState_t controllerStateLeft;
	dmat4 projLeft;
	dmat4 poseLeft;
	
	Pose controllerPoseRight;
	VRControllerState_t controllerStateRight;
	dmat4 projRight;
	dmat4 poseRight;
};

struct OpenVrRecording{

	double t_start;
	vector<Keyframe> frames;
	bool looping = true;
	int currentFrameIndex = 0;

	Keyframe getCurrentFrame(){
		return frames[currentFrameIndex];
	}

	shared_ptr<Buffer> serialize();
	static shared_ptr<OpenVrRecording> deserialize(shared_ptr<Buffer> buffer);


};

class OpenVRHelper {

public:

	shared_ptr<OpenVrRecording> currentRecording = nullptr;
	shared_ptr<OpenVrRecording> currentPlayback = nullptr;

	Keyframe frame;

	double t_start_playing = 0.0;
	
	// vr::VRActiveActionSet_t actionSet = { 0 };
	// actionSet.ulActionSet = m_actionsetDemo;
	vr::VRActionSetHandle_t m_actionsetDemo         = vr::k_ulInvalidActionSetHandle;
	vr::VRActionHandle_t m_actionHideCubes          = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_actionHideThisController = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_actionTriggerHaptic      = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_actionAnalongInput       = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_A                        = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_B                        = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_grip                     = vr::k_ulInvalidActionHandle;

	IVRSystem *system = nullptr;
	string driver = "No Driver";
	string display = "No Display";

	// bitmask of pressed buttons for each device
	uint64_t buttonMap[k_unMaxTrackedDeviceCount];

	// poses for all tracked devices
	TrackedDevicePose_t trackedDevicePose[k_unMaxTrackedDeviceCount];

	vector<dmat4> previousDevicePose = vector<dmat4>(k_unMaxTrackedDeviceCount);
	vector<dmat4> devicePose = vector<dmat4>(k_unMaxTrackedDeviceCount);

	vector<vr::VRControllerState_t> controllerStates = vector<vr::VRControllerState_t>(k_unMaxTrackedDeviceCount);

	Controller left, right;

	dmat4 hmdPose;
	
	const dmat4 flip = dmat4(
		1.0,  0.0, 0.0, 0.0,
		0.0,  0.0, 1.0, 0.0,
		0.0, -1.0, 0.0, 0.0,
		0.0,  0.0, 0.0, 1.0
	);

	static OpenVRHelper *_instance;

	OpenVRHelper() {

	}

	static OpenVRHelper *instance() {
		return OpenVRHelper::_instance;
	}

	shared_ptr<OpenVrRecording> startRecording();
	void stopRecording();
	void playRecording(shared_ptr<OpenVrRecording> recording);
	void stopPlaying();


	bool start();

	void stop();

	bool isActive();

	void processEvents();

	void updatePose(bool enableSync = true);

	void postPresentHandoff();

	Pose getPose(int deviceID);

	Pose getLeftControllerPose();
	Pose getRightControllerPose();

	// RenderModel_t getLeftControllerModel();
	std::pair<RenderModel_t*, RenderModel_TextureMap_t*> getRenderModel(int deviceID);

	vr::VRControllerState_t getLeftControllerState();
	vr::VRControllerState_t getRightControllerState();

	Controller getLeftController();
	Controller getRightController();

	vector<unsigned int> getRecommmendedRenderTargetSize();

	void submit(unsigned int left, unsigned int right);

	void submit(unsigned int texture, EVREye eye, VRTextureBounds_t bounds);

	void submitDistortionApplied(unsigned int left, unsigned int right);

	void submitDistortionApplied(unsigned int texture, EVREye eye);

	DistortionMap computeDistortionMap(EVREye eye, int width, int height);

	//-----------------------------------------------------------------------------
	// code taken from hellovr_opengl_main.cpp
	//-----------------------------------------------------------------------------
	string getTrackedDeviceString(TrackedDeviceIndex_t device, TrackedDeviceProperty prop, TrackedPropertyError *peError = nullptr);

	//-----------------------------------------------------------------------------
	// from hellovr_opengl_main.cpp CMainApplication::GetHMDMatrixProjectionEye()
	//-----------------------------------------------------------------------------
	dmat4 getProjection(EVREye eye, float nearClip, float farClip);
	dmat4 getProjectionVP(EVREye eye, float nearClip, float farClip, int width, int height);

	float getFOV();

	//-----------------------------------------------------------------------------
	// from hellovr_opengl_main.cpp CMainApplication::GetHMDMatrixPoseEye()
	//-----------------------------------------------------------------------------
	dmat4 getEyePose(Hmd_Eye nEye);

	//-----------------------------------------------------------------------------
	// code taken from hellovr_opengl_main.cpp
	//-----------------------------------------------------------------------------
	dmat4 steamToGLM(const HmdMatrix34_t &mSteam);


	dmat4 getHmdPose();
};