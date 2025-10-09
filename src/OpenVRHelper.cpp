
#if !defined(NO_OPENVR)

#include "OpenVRHelper.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <print>
#include "unsuck.hpp"


using std::vector;
using std::cout;
using std::endl;
using std::make_shared;
using std::println;

OpenVRHelper *OpenVRHelper::_instance = new OpenVRHelper();


bool OpenVRHelper::start() {

	if(isActive()) return false;

	if(system){
		return false;
	}

	EVRInitError error;
	system = VR_Init(&error, EVRApplicationType::VRApplication_Scene);

	if (error != VRInitError_None) {
		auto errorMsg = vr::VR_GetVRInitErrorAsEnglishDescription(error);
		cout << "ERROR: could not start VR. (" << error << "): " << errorMsg << endl;

		return false;
	}

	driver = getTrackedDeviceString(k_unTrackedDeviceIndex_Hmd, Prop_TrackingSystemName_String);
	display = getTrackedDeviceString(k_unTrackedDeviceIndex_Hmd, Prop_SerialNumber_String);

	if (!vr::VRCompositor()) {
		cout << "ERROR: failed to initialize compositor." << endl;
		return false;
	}

	// {
	// 	fs::path path = fs::path("./resources/vractions/splatpainter_actions.json");
	// 	string absolutePath = fs::canonical(path).string();

	// 	EVRInputError error = vr::VRInput()->SetActionManifestPath(absolutePath.c_str());

	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/HideCubes", &m_actionHideCubes);
	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/HideThisController", &m_actionHideThisController);
	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/A", &m_A);
	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/B", &m_B);
	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/grip", &m_grip);
	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/TriggerHaptic", &m_actionTriggerHaptic);
	// 	error = vr::VRInput()->GetActionHandle("/actions/demo/in/AnalogInput", &m_actionAnalongInput);

	// 	error = vr::VRInput()->GetActionSetHandle("/actions/demo", &m_actionsetDemo);
	// }

	// {
	// 	EVRSettingsError error;
	// 	VRSettings()->SetBool(vr::k_pch_Dashboard_Section, vr::k_pch_Dashboard_EnableDashboard_Bool, false, &error);
	// 	if(error != VRSettingsError_None) println("Failed to set enableDashboard = false: {}", int(error));
	// 	VRSettings()->SetBool(vr::k_pch_Dashboard_Section, vr::k_pch_Dashboard_ArcadeMode_Bool, true, &error);
	// 	if(error != VRSettingsError_None) println("Failed to set arcadeMode = true: {}", int(error));
	// }

	return true;
}

void OpenVRHelper::stop() {
	if(system){
		VR_Shutdown();
		system = nullptr;
	}
}

bool OpenVRHelper::isActive() {

	if(currentPlayback){
		return true;
	}else{
		return system != nullptr;
	}

}

void OpenVRHelper::submit(unsigned int left, unsigned int right) {

	if(currentPlayback){
		// dont submit to HMD during playback
		return;
	}
	
	submit(left, vr::EVREye::Eye_Left);
	submit(right, vr::EVREye::Eye_Right);
}

void OpenVRHelper::submit(unsigned int texture, EVREye eye, VRTextureBounds_t bounds) {

	if(currentPlayback){
		// dont submit to HMD during playback
		return;
	}


	//VRTextureWithDepth_t texd;
	//texd.handle = (void*)texture;
	//texd.eType = ETextureType::TextureType_OpenGL;
	//texd.eColorSpace = vr::ColorSpace_Gamma;
	//texd.depth.



	Texture_t tex = { (void*)texture, ETextureType::TextureType_OpenGL, vr::ColorSpace_Gamma };

	

	// vr::VRTextureBounds_t bounds;
	// bounds.uMin = 0.4f;
	// bounds.uMax = 0.5f;
	// bounds.vMin = 0.4f;
	// bounds.vMax = 0.5f;

	// EVRSubmitFlags

	auto flags = vr::EVRSubmitFlags::Submit_Default;
	VRCompositor()->Submit(eye, &tex, &bounds);
}

void OpenVRHelper::submitDistortionApplied(unsigned int left, unsigned int right) {

	if(currentPlayback){
		// dont submit to HMD during playback
		return;
	}

	submitDistortionApplied(left, vr::EVREye::Eye_Left);
	submitDistortionApplied(right, vr::EVREye::Eye_Right);
}

void OpenVRHelper::submitDistortionApplied(unsigned int texture, EVREye eye) {

	if(currentPlayback){
		// dont submit to HMD during playback
		return;
	}

	Texture_t tex = { (void*)texture, ETextureType::TextureType_OpenGL, vr::ColorSpace_Gamma };

	vr::VRTextureBounds_t *bounds = (vr::VRTextureBounds_t*)0;
	auto flags = vr::Submit_LensDistortionAlreadyApplied;
	VRCompositor()->Submit(eye, &tex, bounds, flags);
}

DistortionMap OpenVRHelper::computeDistortionMap(EVREye eye, int width, int height) {

	int numPixels = width * height;

	DistortionMap map;
	map.red = vector<UV>(numPixels);
	map.green = vector<UV>(numPixels);
	map.blue = vector<UV>(numPixels);

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {

			float u = float(x) / float(width - 1);
			float v = float(y) / float(height - 1);

			vr::DistortionCoordinates_t outCoordinatates;

			system->ComputeDistortion(eye, u, v, &outCoordinatates);

			int targetIndex = width * y + x;

			map.red[targetIndex] = { outCoordinatates.rfRed[0], outCoordinatates.rfRed[1] };
			map.green[targetIndex] = { outCoordinatates.rfGreen[0], outCoordinatates.rfGreen[1] };
			map.blue[targetIndex] = { outCoordinatates.rfBlue[0], outCoordinatates.rfBlue[1] };
		}
	}

	return map;
}


void OpenVRHelper::processEvents() {

	if(currentPlayback){

		double ellapsedSinceStart = now() - t_start_playing;

		while(currentPlayback->getCurrentFrame().timestamp < ellapsedSinceStart){
			currentPlayback->currentFrameIndex++;

			if(currentPlayback->currentFrameIndex >= currentPlayback->frames.size()){
				currentPlayback->currentFrameIndex = 0;
				t_start_playing = now();
				ellapsedSinceStart = now() - t_start_playing;

				if(!currentPlayback->looping){
					stopPlaying();
				}
			}
		}


		return; 
	}

	VREvent_t event;
	while (system->PollNextEvent(&event, sizeof(event))) {
		// ProcessVREvent(event);
	}

	// vr::VRActiveActionSet_t actionSet = { 0 };
	// actionSet.ulActionSet = m_actionsetDemo;
	// auto error = vr::VRInput()->UpdateActionState( &actionSet, sizeof(actionSet), 1 );


	// {
	// 	vr::InputDigitalActionData_t actionData;
	// 	error = vr::VRInput()->GetDigitalActionData(m_actionHideCubes, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle );

	// 	bool hideCubes = actionData.bActive && actionData.bState;

	// 	if(hideCubes){
	// 		static int counter = 0;
	// 		println("hide cubes, {}", counter);

	// 		counter++;
	// 	}
	// }

	// {
	// 	vr::InputDigitalActionData_t actionData;
	// 	error = vr::VRInput()->GetDigitalActionData(m_A, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle );

	// 	bool actionActive = actionData.bActive && actionData.bState;

	// 	if(actionActive){
	// 		static int counter = 0;
	// 		println("A, {}", counter);

	// 		counter++;
	// 	}
	// }

	// {
	// 	vr::InputDigitalActionData_t actionData;
	// 	error = vr::VRInput()->GetDigitalActionData(m_B, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle );

	// 	bool actionActive = actionData.bActive && actionData.bState;

	// 	if(actionActive){
	// 		static int counter = 0;
	// 		println("B, {}", counter);

	// 		counter++;
	// 	}
	// }

	// {
	// 	vr::InputDigitalActionData_t actionData;
	// 	error = vr::VRInput()->GetDigitalActionData(m_grip, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle );

	// 	bool actionActive = actionData.bActive && actionData.bState;

	// 	if(actionActive){
	// 		static int counter = 0;
	// 		println("Grip, {}", counter);

	// 		counter++;
	// 	}
	// }

	// VRInput()->ShowActionOrigins(m_actionsetDemo, m_A);

	//VRInput()->ShowBindingsForActionSet(&actionSet, sizeof(actionSet), 1, 0);
	
	// if(GetDigitalActionState(m_actionHideCubes)){
	// 	println("hide cubes");
	// }






	vector<TrackedDeviceIndex_t> triggered;

	for (vr::TrackedDeviceIndex_t unDevice = 0; unDevice < vr::k_unMaxTrackedDeviceCount; unDevice++) {
		vr::VRControllerState_t &state = controllerStates[unDevice];
		if (system->GetControllerState(unDevice, &state, sizeof(state))) {

			auto previousState = buttonMap[unDevice];
			auto currentState = state.ulButtonPressed;

			uint64_t justPressed = (previousState ^ currentState) & currentState;
			uint64_t justReleased = (previousState ^ currentState) & previousState;

			// if(state.ulButtonPressed != 0llu){
			// 	println("{:10}, {:10}, <{:4.1f}, {:4.1f}>, <{:4.1f}, {:4.1f}>", 
			// 		state.ulButtonPressed, state.ulButtonTouched, 
			// 		state.rAxis[0].x, state.rAxis[0].y,
			// 		state.rAxis[1].x, state.rAxis[1].y
			// 	);
			// }

			buttonMap[unDevice] = state.ulButtonPressed;

		}
	}

	{
		left.previous.state = left.state;
		left.previous.valid = left.valid;
		left.previous.transform = left.transform;

		Pose pose = getLeftControllerPose();
		left.state = getLeftControllerState();
		left.valid = pose.valid;
		left.transform = pose.transform;
	}

	{
		right.previous.state = right.state;
		right.previous.valid = right.valid;
		right.previous.transform = right.transform;

		Pose pose = getRightControllerPose();
		right.state = getRightControllerState();
		right.valid = pose.valid;
		right.transform = pose.transform;
	}

	// {
	// 	int leftHandID = system->GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
	// 	int rightHandID = system->GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole::TrackedControllerRole_RightHand);

	// 	{ // GRIP
	// 		vr::InputDigitalActionData_t actionData;
	// 		error = vr::VRInput()->GetDigitalActionData(m_grip, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle );

	// 		bool actionActive = actionData.bActive && actionData.bState;

	// 		if(actionActive){

	// 			auto& state = controllerStates[rightHandID];
	// 			state.ulButtonPressed = state.ulButtonPressed | 4;


	// 			static int counter = 0;
	// 			println("Grip, {}", counter);

	// 			counter++;
	// 		}
	// 	}

	// 	{ // GRIP analog
	// 		InputAnalogActionData_t actionData;
	// 		error = vr::VRInput()->GetAnalogActionData(m_grip, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle );

	// 		if(actionData.bActive){

	// 			auto& state = controllerStates[rightHandID];
	// 			state.ulButtonPressed = state.ulButtonPressed | 4;


	// 			static int counter = 0;
	// 			println("Grip, {}, {}, {}", actionData.x, actionData.y, actionData.z);

	// 			counter++;
	// 		}
	// 	}
	// }

	auto size = getRecommmendedRenderTargetSize();
	float near = 0.01;
	float far = 100'000.0;

	
	frame.poseHMD               = this->hmdPose;
	frame.width = size[0];
	frame.height = size[1];

	frame.controllerPoseLeft    = getLeftControllerPose();
	frame.controllerStateLeft   = getLeftControllerState();
	frame.projLeft              = getProjection(vr::EVREye::Eye_Left, near, far);
	frame.poseLeft              = getEyePose(vr::Hmd_Eye::Eye_Left);

	frame.controllerPoseRight   = getRightControllerPose();
	frame.controllerStateRight  = getRightControllerState();
	frame.projRight             = getProjection(vr::EVREye::Eye_Right, near, far);
	frame.poseRight             = getEyePose(vr::Hmd_Eye::Eye_Right);

	// generate keyframe
	if(currentRecording){
		frame.timestamp = now() - currentRecording->t_start;
		currentRecording->frames.push_back(frame);
	}
}


void OpenVRHelper::updatePose(bool enableSync) {
	if (!system) return;
	if(currentPlayback) return;


	if(enableSync){
		VRCompositor()->WaitGetPoses(trackedDevicePose, k_unMaxTrackedDeviceCount, NULL, 0);
	}else{
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, 0.0, trackedDevicePose, k_unMaxTrackedDeviceCount);
	}

	for (int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice) {
	
		if (trackedDevicePose[nDevice].bPoseIsValid) {
			previousDevicePose[nDevice] = devicePose[nDevice];
			devicePose[nDevice] = steamToGLM(trackedDevicePose[nDevice].mDeviceToAbsoluteTracking);
		}
	}
	
	if (trackedDevicePose[k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
		hmdPose = devicePose[k_unTrackedDeviceIndex_Hmd];
	}

}

void OpenVRHelper::postPresentHandoff() {

	if(currentPlayback) return;

	// VRCompositor()->PostPresentHandoff();
};

Pose OpenVRHelper::getPose(int deviceID) {

	if (deviceID < 0) {
		return{ false, glm::dmat4() };
	}

	if (trackedDevicePose[deviceID].bPoseIsValid) {
		return { true, devicePose[deviceID] };
	} else {
		return{ false, glm::dmat4() };
	}
}

Pose OpenVRHelper::getLeftControllerPose() {

	if(currentPlayback){
		return currentPlayback->getCurrentFrame().controllerPoseLeft;
	}

	int leftHandID = system->GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);

	return getPose(leftHandID);
}

std::pair<RenderModel_t*, RenderModel_TextureMap_t*> OpenVRHelper::getRenderModel(int deviceID){

	static unordered_map<int, std::pair<RenderModel_t*, RenderModel_TextureMap_t*>> cache;

	if(cache.find(deviceID) == cache.end()){
		// if model is not in cache, check if we can load it. If not, stick with default model.

		RenderModel_t* model = nullptr;

		string sRenderModelName = getTrackedDeviceString(deviceID, vr::Prop_RenderModelName_String);
		EVRRenderModelError error = VRRenderModels()->LoadRenderModel_Async(sRenderModelName.c_str(), &model);

		if(error == VRRenderModelError_Loading){
			return {nullptr, nullptr};
		}else if(error == VRRenderModelError_None){

			RenderModel_TextureMap_t *texture = nullptr;
			EVRRenderModelError error = VRRenderModels()->LoadTexture_Async(model->diffuseTextureId, &texture);

			if(error == VRRenderModelError_Loading){
				return {nullptr, nullptr};
			}else{
				cache[deviceID] = {model, texture};
			
				return {model, texture};
			}
		}

	}else{
		return cache[deviceID];
	}
}

Pose OpenVRHelper::getRightControllerPose() {

	if(currentPlayback){
		return currentPlayback->getCurrentFrame().controllerPoseRight;
	}

	int rightHandID = system->GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole::TrackedControllerRole_RightHand);

	return getPose(rightHandID);
}

vr::VRControllerState_t OpenVRHelper::getLeftControllerState(){

	if(currentPlayback){
		return currentPlayback->getCurrentFrame().controllerStateLeft;
	}

	int leftHandID = system->GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);

	if(leftHandID < 0){
		return vr::VRControllerState_t();
	}

	vr::VRControllerState_t state = controllerStates[leftHandID];

	return state;
}

vr::VRControllerState_t OpenVRHelper::getRightControllerState(){

	if(currentPlayback){
		return currentPlayback->getCurrentFrame().controllerStateRight;
	}

	int rightHandID = system->GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole::TrackedControllerRole_RightHand);

	if(rightHandID < 0){
		return vr::VRControllerState_t();
	}

	vr::VRControllerState_t state = controllerStates[rightHandID];

	return state;
}

Controller OpenVRHelper::getLeftController(){
	return left;
}

Controller OpenVRHelper::getRightController(){
	return right;
}

vector<unsigned int> OpenVRHelper::getRecommmendedRenderTargetSize(){

	if(currentPlayback){
		auto frame = currentPlayback->getCurrentFrame();
		return {uint32_t(frame.width), uint32_t(frame.height)};
	}

	uint32_t width;
	uint32_t height;
	system->GetRecommendedRenderTargetSize(&width, &height);

	return { width, height };
}


//-----------------------------------------------------------------------------
// code taken from hellovr_opengl_main.cpp
//-----------------------------------------------------------------------------
string OpenVRHelper::getTrackedDeviceString(TrackedDeviceIndex_t device, TrackedDeviceProperty prop, TrackedPropertyError *peError) {
	uint32_t unRequiredBufferLen = system->GetStringTrackedDeviceProperty(device, prop, nullptr, 0, peError);
	if (unRequiredBufferLen == 0)
		return "";

	char *pchBuffer = new char[unRequiredBufferLen];
	unRequiredBufferLen = system->GetStringTrackedDeviceProperty(device, prop, pchBuffer, unRequiredBufferLen, peError);
	std::string sResult = pchBuffer;
	delete[] pchBuffer;

	return sResult;
}


//-----------------------------------------------------------------------------
// from hellovr_opengl_main.cpp CMainApplication::GetHMDMatrixProjectionEye()
// and https://github.com/ValveSoftware/openvr/wiki/IVRSystem::GetProjectionRaw
//-----------------------------------------------------------------------------
dmat4 OpenVRHelper::getProjection(EVREye eye, float nearClip, float farClip) {

	if(currentPlayback){
		auto frame = currentPlayback->getCurrentFrame();

		if(eye == vr::Hmd_Eye::Eye_Left) return frame.projLeft;
		if(eye == vr::Hmd_Eye::Eye_Right) return frame.projRight;
	}

	if (!system) {
		return dmat4();
	}

	float left = 0;
	float right = 0;
	float top = 0;
	float bottom = 0;
	system->GetProjectionRaw(eye, &left, &right, &top, &bottom);

	// println("left and right: {:.3f}, {:.3f}, top and bottom: {:.3f}, {:.3f}", left, right, top, bottom);

	float idx = 1.0f / (right - left);
	float idy = 1.0f / (bottom - top);
	float sx = right + left;
	float sy = bottom + top;
	float zFar = farClip;
	float zNear = nearClip;

	// reverse z with infinite far

	auto customProj = glm::dmat4(
		2.0 * idx , 0.0       , 0.0    , 0.0,
		0.0       , 2.0 * idy , 0.0    , 0.0,
		sx * idx  , sy * idy  , 0.0    , -1.0,
		0.0       , 0.0       , zNear  , 0.0
	);

	return customProj;
}

// Different projection matrix for perspective correct gaussians
dmat4 OpenVRHelper::getProjectionVP(EVREye eye, float nearClip, float farClip, int width, int height) {

	if (!system) {
		return dmat4();
	}

	float left = 0;
	float right = 0;
	float top = 0;
	float bottom = 0;
	system->GetProjectionRaw(eye, &left, &right, &top, &bottom);

	float idx = 1.0f / (right - left);
	float idy = 1.0f / (bottom - top);
	float sx = right + left;
	float sy = bottom + top;
	float zFar = farClip;
	float zNear = nearClip;

	auto customProj = glm::dmat4(
		2.0 * idx, 0.0, 0.0, 0.0,
		0.0, 2.0 * idy, 0.0, 0.0,
		sx * idx, sy * idy, -(zFar + zNear) / (zFar - zNear), -1.0,
		0.0, 0.0, -2.0 * zFar * zNear / (zFar - zNear), 0.0
	);

	auto V = glm::dmat4(
		width / 2.0, 0.0, 0.0, 0.0,
		0.0, height / 2.0, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		(width - 1) / 2.0, (height - 1) / 2.0, 0.0, 1.0
	);

	auto VP = V * customProj;

	return VP;
}


float OpenVRHelper::getFOV() {
	float l_left = 0.0f, l_right = 0.0f, l_top = 0.0f, l_bottom = 0.0f;
	system->GetProjectionRaw(vr::EVREye::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);

	// top and bottom seem reversed. Asume larger value to be the top.
	// see https://github.com/ValveSoftware/openvr/issues/110
	float realTop = std::max(l_top, l_bottom);

	return 2.0f * atan2(realTop, 1.0);
}

dmat4 OpenVRHelper::getHmdPose() {

	if(currentPlayback){
		return currentPlayback->getCurrentFrame().poseHMD;
	}

	return hmdPose;
}

//-----------------------------------------------------------------------------
// from hellovr_opengl_main.cpp CMainApplication::GetHMDMatrixPoseEye()
//-----------------------------------------------------------------------------
dmat4 OpenVRHelper::getEyePose(Hmd_Eye nEye) {

	if(currentPlayback){
		auto frame = currentPlayback->getCurrentFrame();

		if(nEye == vr::Hmd_Eye::Eye_Left) return frame.poseLeft;
		if(nEye == vr::Hmd_Eye::Eye_Right) return frame.poseRight;
	}

	if (!system) {
		return glm::dmat4();
	}

	HmdMatrix34_t mat = system->GetEyeToHeadTransform(nEye);

	dmat4 matrix(
		mat.m[0][0], mat.m[1][0], mat.m[2][0], 0.0,
		mat.m[0][1], mat.m[1][1], mat.m[2][1], 0.0,
		mat.m[0][2], mat.m[1][2], mat.m[2][2], 0.0,
		mat.m[0][3], mat.m[1][3], mat.m[2][3], 1.0f
	);

	return matrix;
}


dmat4 OpenVRHelper::steamToGLM(const HmdMatrix34_t &mSteam) {

	dmat4 matrix{
		mSteam.m[0][0], mSteam.m[1][0], mSteam.m[2][0], 0.0,
		mSteam.m[0][1], mSteam.m[1][1], mSteam.m[2][1], 0.0,
		mSteam.m[0][2], mSteam.m[1][2], mSteam.m[2][2], 0.0,
		mSteam.m[0][3], mSteam.m[1][3], mSteam.m[2][3], 1.0f
	};

	return matrix;
}


shared_ptr<OpenVrRecording> OpenVRHelper::startRecording(){
	auto recording = make_shared<OpenVrRecording>();
	recording->t_start = now();

	this->currentRecording = recording;

	return recording;
}

void OpenVRHelper::stopRecording(){
	this->currentRecording = nullptr;
}

void OpenVRHelper::playRecording(shared_ptr<OpenVrRecording> recording){
	this->currentPlayback = recording;
	t_start_playing = now();
}

void OpenVRHelper::stopPlaying(){
	this->currentPlayback = nullptr;
}

shared_ptr<Buffer> OpenVrRecording::serialize(){

	uint64_t bufferSize = frames.size() * sizeof(Keyframe);

	auto buffer = make_shared<Buffer>(bufferSize);

	memcpy(buffer->data, frames.data(), bufferSize);

	return buffer;
}

shared_ptr<OpenVrRecording> OpenVrRecording::deserialize(shared_ptr<Buffer> buffer){

	int numFrames = buffer->size / sizeof(Keyframe);

	auto recording = make_shared<OpenVrRecording>();
	recording->t_start = 0;
	recording->looping = true;
	recording->currentFrameIndex = 0;
	recording->frames.resize(numFrames);

	memcpy(recording->frames.data(), buffer->data, buffer->size);

	return recording;
}

#endif