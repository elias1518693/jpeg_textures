
void SplatEditor::makeStats(){
	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;

	if(settings.showStats){

		ImVec2 kernelWindowSize = {720, 660};
		ImGui::SetNextWindowPos({10, 100}, ImGuiCond_Once);
		ImGui::SetNextWindowSize(kernelWindowSize, ImGuiCond_Once);

		if(ImGui::Begin("Stats")){

			static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
			ImGui::Text("Stats: ");
			if (ImGui::BeginTable("Stats", 2, flags)){

				// HEADER
				ImGui::TableSetupColumn("Label");
				ImGui::TableSetupColumn("Value");
				ImGui::TableHeadersRow();

				{ // FPS
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("FPS");
					ImGui::TableSetColumnIndex(1);
					string str = format("{:.1f}", GLRenderer::fps);
					ImGui::TextUnformatted(str.c_str());
				}

				{ // RENDERED TRIANGLES
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("rendered triangles");

					ImGui::TableSetColumnIndex(1);
					string str = format(getSaneLocale(),"{:L}", Runtime::numRenderedTriangles);
					ImGui::TextUnformatted(str.c_str());
				}

				{ // Key
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("Recent Key Input");

					static string str = " ";
					if(Runtime::frame_keys.size() > 0){
						int key = Runtime::frame_keys[0];
						int scancode = Runtime::frame_actions[0];
						int action = Runtime::frame_mods[0];
						str = format("key: {}, code: {}, action: {}", key, scancode, action);
					}

					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(str.c_str());
				}

				{ // Mouse
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("Mouse Position");

					string str = format("{:5} - {:5}", 
						Runtime::mouseEvents.pos_x,
						Runtime::mouseEvents.pos_y
					);

					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(str.c_str());
				}

				for(auto [label, value] : Runtime::debugValues){
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(value.c_str());
				}

				for(auto [label, value] : Runtime::debugValueList){
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(value.c_str());
				}
				
				ImGui::EndTable();
			}

			//=======================================================
			//=======================================================
			//=======================================================
			ImGui::Text(" ");
			ImGui::Text("More Stats: ");
			if (ImGui::BeginTable("Settings", 2, flags)){

				// HEADER
				ImGui::TableSetupColumn("Label");
				ImGui::TableSetupColumn("Value");
				ImGui::TableHeadersRow();

				{ // FOVY
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("fovy");

					float deg = GLRenderer::camera->fovy;
					float rad = 3.1415 * deg / 180.0;
					ImGui::TableSetColumnIndex(1);
					string str = format("{:.1f} deg / {:.4f} rad", deg, rad);
					ImGui::TextUnformatted(str.c_str());
				}

				{ // ASPECT
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("aspect");

					ImGui::TableSetColumnIndex(1);
					string str = format("{:.3f}", GLRenderer::camera->aspect);
					ImGui::TextUnformatted(str.c_str());
				}
				
				{ // NEAR
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("near");

					ImGui::TableSetColumnIndex(1);
					string str = format("{:.3f}", GLRenderer::camera->near);
					ImGui::TextUnformatted(str.c_str());
				}

				{ // Framebuffer Size
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("framebuffer size");

					ImGui::TableSetColumnIndex(1);
					string str = format("{} x {}", GLRenderer::width, GLRenderer::height);
					ImGui::TextUnformatted(str.c_str());
				}
				

				ImGui::EndTable();
			}

		}

		ImGui::End();
	
	}

}