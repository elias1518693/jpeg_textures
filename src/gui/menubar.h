
void SplatEditor::makeMenubar(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& scene = editor->scene;

	if (ImGui::BeginMainMenuBar()){

		ImGui::MenuItem(" ##hidebuttondoesntworkwithoutthis", "");

		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 65);

		if(ImGui::MenuItem("Hide GUI", "")){
			settings.hideGUI = !settings.hideGUI;
		}

		ImGui::EndMainMenuBar();
	}

}