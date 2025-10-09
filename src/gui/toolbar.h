

// see https://github.com/ocornut/imgui/issues/2648
void SplatEditor::makeToolbar(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& scene = editor->scene;
	auto drawlist = ImGui::GetForegroundDrawList();

	ImVec2 toolbar_start = ImVec2(0, 19);

	ImGui::SetNextWindowPos(toolbar_start);
	ImVec2 requested_size = ImVec2(GLRenderer::width, 0.0f);
	ImGui::SetNextWindowSize(requested_size);

	struct Section{
		float x_start;
		float x_end;
		string label;
	};

	vector<Section> sections;

	auto startSection = [&](string label){
		ImGui::SameLine();

		Section section;
		section.x_start = ImGui::GetCursorPosX();
		section.label = label;

		sections.push_back(section);
	};

	auto endSection = [&](){
		ImGui::SameLine();
		float x = ImGui::GetCursorPosX();
		ImU32 color = IM_COL32(255, 255, 255, 75);
		drawlist->AddLine({x - 4.0f, 51.0f - 32.0f}, {x - 4.0f, 120.0f - 32.0f}, color, 1.0f);

		Section& section = sections[sections.size() - 1];
		section.x_end = x;
	};

	auto startHighlightButtonIf = [&](bool condition){
		ImGuiStyle* style = &ImGui::GetStyle();
		ImVec4* colors = style->Colors;
		ImVec4 color = colors[ImGuiCol_Button];

		if(condition){
			color = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
		}
		ImGui::PushStyleColor(ImGuiCol_Button, color);
	};

	auto endHighlightButtonIf = [&](){
		ImGui::PopStyleColor(1);
	};
	
	uint32_t flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
	ImGui::Begin("Toolbar", nullptr, flags);

	
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
	{
		ImVec2 buttonSize = ImVec2(32.0f, 32.0f);
		float symbolSize = 32.0f;
		ImVec4 bg_col = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		float iconSize = 16.0f;

		struct Box2{
			ImVec2 start;
			ImVec2 end;
		};

		{ // WIDGETS
			startSection("Widgets");

			ImGui::SameLine();
			ImGui::BeginGroup();

			float cx = ImGui::GetCursorPosX();
			float cy = ImGui::GetCursorPosY();

			// startHighlightButtonIf(editor->settings.showDevStuff);
			// if(ImGui::Button("Dev&Debug")){
			// 	editor->settings.showDevStuff = !editor->settings.showDevStuff;
			// }
			// endHighlightButtonIf();

			ImGui::SameLine();
			// startHighlightButtonIf(editor->settings.showKernelInfos);
			// if(ImGui::Button("Kernels")){
			// 	editor->settings.showKernelInfos = !editor->settings.showKernelInfos;
			// }
			// endHighlightButtonIf();

			ImGui::SameLine();
			startHighlightButtonIf(editor->settings.showMemoryInfos);
			if(ImGui::Button("Memory")){
				editor->settings.showMemoryInfos = !editor->settings.showMemoryInfos;
			}
			endHighlightButtonIf();

			ImGui::SameLine();
			startHighlightButtonIf(editor->settings.showTimingInfos);
			if(ImGui::Button("Timings")){
				editor->settings.showTimingInfos = !editor->settings.showTimingInfos;
			}
			endHighlightButtonIf();

			startHighlightButtonIf(editor->settings.showStats);
			// ImGui::SameLine();
			if(ImGui::Button("Stats")){
				editor->settings.showStats = !editor->settings.showStats;
			}
			endHighlightButtonIf();

			ImGui::EndGroup();

			endSection();
		}

		{ // DEV
			startSection("Dev");

			ImGui::BeginGroup();

			ImGui::SameLine();

			float cx = ImGui::GetCursorPosX();
			float cy = ImGui::GetCursorPosY();

			ImGui::Text("Render: ");

			static int mode = 0;
			ImGui::SameLine();
			ImGui::RadioButton("Texture##rendermode", &mode, 0); 
			ImGui::SameLine();
			ImGui::RadioButton("UVs##rendermode", &mode, 1); 
			ImGui::SameLine();
			ImGui::RadioButton("MCUs##rendermode", &mode, 2);
			ImGui::SameLine();
			ImGui::RadioButton("TexID##rendermode", &mode, 3);
			ImGui::SameLine();
			ImGui::RadioButton("Mip Level##rendermode", &mode, 5);
			ImGui::SameLine();
			ImGui::RadioButton("Caching##rendermode", &mode, 4);

			editor->settings.showUVs = mode == 1;
			editor->settings.showMCUs = mode == 2;
			editor->settings.showTexID = mode == 3;
			editor->settings.showCaching = mode == 4;
			editor->settings.showMipLevel = mode == 5;

			ImGui::SetCursorPosX(cx);
			ImGui::SetCursorPosY(cy + 24);

			ImGui::Checkbox("Linear Interpolation", &editor->settings.enableLinearInterpolation);
			ImGui::SameLine();
			ImGui::Checkbox("Mip Mapping", &editor->settings.enableMipMapping);
			ImGui::SameLine();
			ImGui::Checkbox("Texture Block Caching", &editor->settings.enableTextureBlockCaching);
			ImGui::SameLine();
			ImGui::Checkbox("Freeze Cache", &editor->settings.freezeCache);
			
			// ImGui::SameLine();
			// ImGui::Checkbox("use JPEG", &editor->settings.useJpegRendering);

			// static int jpegMode = 0;
			// // ImGui::SameLine();
			// ImGui::RadioButton("CUDA (jpeg)##jpegOrGl", &jpegMode, 0);
			// ImGui::SameLine();
			// ImGui::RadioButton("OpenGL (uncompressed)##jpegOrGl", &jpegMode, 1);
			// editor->settings.useJpegRendering = jpegMode == 0;
			
			
			ImGui::EndGroup();
			endSection();
		}

		{ // STATS
			startSection("Stats");

			ImGui::BeginGroup();

			ImGui::SameLine();

			float cx = ImGui::GetCursorPosX();
			float cy = ImGui::GetCursorPosY();

			
			static double lastChange = now();
			static uint32_t lastLargest = 0;

			
			if(editor->deviceState.numMcusDecoded >= lastLargest){
				lastLargest = editor->deviceState.numMcusDecoded;
				lastChange = now();
			}
			if(now() - lastChange > 1.0){
				lastLargest = 0.0;
				lastChange = now();
			}

			string strMcus = format(getSaneLocale(), "MCUs: {:10L}", lastLargest);
			ImGui::Text(strMcus.c_str());
			
			
			ImGui::EndGroup();
			endSection();
		}

		{ // MISC
			startSection("Misc");

			ImGui::BeginGroup();

			ImGui::SameLine();

			float cx = ImGui::GetCursorPosX();
			float cy = ImGui::GetCursorPosY();

			
			ImGui::SameLine();
			if(ImGui::Button("Copy Camera")){

				auto pos = Runtime::controls->getPosition();
				auto target = Runtime::controls->target;

				stringstream ss;
				ss << format("// position: {}, {}, {} \n", pos.x, pos.y, pos.z);
				ss << format("controls.yaw    = {:.3f};\n", Runtime::controls->yaw);
				ss << format("controls.pitch  = {:.3f};\n", Runtime::controls->pitch);
				ss << format("controls.radius = {:.3f};\n", Runtime::controls->radius);
				ss << format("controls.target = {{ {:.3f}, {:.3f}, {:.3f}, }};\n", target.x, target.y, target.z);

				string str = ss.str();

				glfwSetClipboardString(nullptr, str.c_str());
			}

			ImGui::SameLine();
			if(ImGui::Button("RotLeft20")){
				OrbitControls* controls = Runtime::controls;
				
				dvec3 dir = controls->getDirection();
				double angleDeg = 20.0;
				double angleRad = 3.1415 * angleDeg / 180.0;
				dmat4 rot = glm::rotate(angleRad, dvec3{0.0, 0.0, 1.0});

				dvec3 newDir = rot * dvec4{dir.x, dir.y, dir.z, 0.0};
				dvec3 pos = controls->getPosition();
				dvec3 newTarget = pos + newDir * controls->radius;

				controls->target = newTarget;
				controls->yaw = controls->yaw + angleRad;
			}
			ImGui::SameLine();
			if(ImGui::Button("RotRight20")){
				OrbitControls* controls = Runtime::controls;
				
				dvec3 dir = controls->getDirection();
				double angleDeg = -20.0;
				double angleRad = 3.1415 * angleDeg / 180.0;
				dmat4 rot = glm::rotate(angleRad, dvec3{0.0, 0.0, 1.0});

				dvec3 newDir = rot * dvec4{dir.x, dir.y, dir.z, 0.0};
				dvec3 pos = controls->getPosition();
				dvec3 newTarget = pos + newDir * controls->radius;

				controls->target = newTarget;
				controls->yaw = controls->yaw + angleRad;
			}

			ImGui::SameLine();
			if(ImGui::Button("RotBench")){

				editor->settings.rotBenchEnabled = !editor->settings.rotBenchEnabled;
				// OrbitControls* controls = Runtime::controls;
				
				// dvec3 dir = controls->getDirection();
				// double angleDeg = 20.0;
				// double angleRad = 3.1415 * angleDeg / 180.0;
				// dmat4 rot = glm::rotate(angleRad, dvec3{0.0, 0.0, 1.0});

				// dvec3 newDir = rot * dvec4{dir.x, dir.y, dir.z, 0.0};
				// dvec3 pos = controls->getPosition();
				// dvec3 newTarget = pos + newDir * controls->radius;

				// controls->target = newTarget;
				// controls->yaw = controls->yaw + angleRad;
			}
			ImGui::SameLine();

			{ // TOGGLE VR
				if(ImGui::Button("VR")){

					
					auto ovr = OpenVRHelper::instance();
					if(ovr->isActive()){
						ovr->stop();
					}else{
						ovr->start();
					}
				}
			}
			
			
			ImGui::EndGroup();
			endSection();
		}


		ImGui::Text(" ");

	}

	ImGui::PopStyleColor(1);

	ImVec2 wpos = ImGui::GetWindowPos();
	ImVec2 toolbar_end = ImVec2{wpos.x + ImGui::GetWindowWidth(), wpos.y + ImGui::GetWindowHeight()};
	// ImGui::GetForegroundDrawList()->AddRect( start, end, IM_COL32( 255, 255, 0, 255 ) );


	ImGui::End();

	{ // TOOLBAR SECTION LABELS
		uint32_t flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
		ImGui::Begin("Toolbar", nullptr, flags);

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 1.0f, 1.0f, 0.5f});

		for(Section section : sections){

			float x_center = (section.x_end + section.x_start) / 2.0f;
			float width = ImGui::CalcTextSize(section.label.c_str()).x;
			float x = x_center - width / 2.0f;

			ImGui::SetCursorPosX(x);
			ImGui::Text(section.label.c_str());
			ImGui::SameLine();

		}

		ImGui::Text(" ");

		ImGui::PopStyleColor(1);

		ImGui::End();
	}

}