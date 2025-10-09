

void alignRight(string text) {
	float rightBorder = ImGui::GetCursorPosX() + ImGui::GetColumnWidth();
	float width = ImGui::CalcTextSize(text.c_str()).x;
	ImGui::SetCursorPosX(rightBorder - width);
}

void makeKernels(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;

	if(settings.showKernelInfos){

		ImVec2 kernelWindowSize = {800, 600};
		ImGui::SetNextWindowPos({
			(GLRenderer::width - kernelWindowSize.x) / 2, 
			(GLRenderer::height - kernelWindowSize.y) / 2, }, 
			ImGuiCond_Once);
		ImGui::SetNextWindowSize(kernelWindowSize, ImGuiCond_Once);

		bool open;
		if(ImGui::Begin("Kernels", &open)){

			static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;


			auto printKernels = [&](string label, CudaModularProgram* program){

				string strlabel = format("## {}", label);
				ImGui::Text("===============================");
				ImGui::Text(strlabel.c_str());
				ImGui::Text("===============================");
				
				if(ImGui::BeginTable("Kernels##listOfKernels", 5, flags))
				{
					ImGui::TableSetupColumn("Name",       ImGuiTableColumnFlags_WidthStretch, 3.0f);
					ImGui::TableSetupColumn("registers",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGui::TableSetupColumn("shared mem", ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGui::TableSetupColumn("max threads/block", ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGui::TableSetupColumn("blocks(256t)/SM", ImGuiTableColumnFlags_WidthStretch, 1.0f);

					ImGui::TableHeadersRow();

					for(auto [name, function] : program->kernels){
						ImGui::TableNextRow();
				
						int maxThreadsPerBlock = 0;
						int registersPerThread;
						int sharedMemory;
						cuFuncGetAttribute(&maxThreadsPerBlock, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, function);
						cuFuncGetAttribute(&registersPerThread, CU_FUNC_ATTRIBUTE_NUM_REGS, function);
						cuFuncGetAttribute(&sharedMemory, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, function);

						int numBlocksPerSM;
						cuOccupancyMaxActiveBlocksPerMultiprocessor (&numBlocksPerSM, function, 256, 0);

						string strThreadsPerBlock = format("{}", maxThreadsPerBlock);
						if(maxThreadsPerBlock == 0) strThreadsPerBlock = "?";
						string strRegisters = format("{}", registersPerThread);
						string strSharedMem = format(getSaneLocale(), "{:L}", sharedMemory);
						string strBlocksPerSM = format(getSaneLocale(), "{:L}", numBlocksPerSM);

						ImGui::TableNextColumn();
						ImGui::Text(name.c_str());

						ImGui::TableNextColumn();
						alignRight(strRegisters);
						ImGui::Text(strRegisters.c_str());

						ImGui::TableNextColumn();
						alignRight(strSharedMem);
						ImGui::Text(strSharedMem.c_str());

						ImGui::TableNextColumn();
						alignRight(strThreadsPerBlock);
						ImGui::Text(strThreadsPerBlock.c_str());

						ImGui::TableNextColumn();
						alignRight(strBlocksPerSM);
						ImGui::Text(strBlocksPerSM.c_str());
					}

					ImGui::EndTable();
				}
			};

			printKernels("HELPERS", editor->prog_jpeg);
		}

		settings.showKernelInfos = open;

		ImGui::End();
	}

}

void makeMemory(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;

	if(settings.showMemoryInfos){

		// auto windowSize = ImGui::GetWindowSize();
		ImVec2 windowSize = {800, 600};
		ImGui::SetNextWindowPos({
			(GLRenderer::width - windowSize.x) / 2, 
			(GLRenderer::height - windowSize.y) / 2, }, 
			ImGuiCond_Once);
		ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);

		static bool open = true;
		if(ImGui::Begin("Memory", &open)){

			static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;

			ImGui::Text("List of allocations made via CURuntime::alloc and allocVirtual.");
			ImGui::Text(" ");

			ImGui::Text("===============================");
			ImGui::Text("## CUDA MEMORY ALLOCATIONS");
			ImGui::Text("===============================");

			if(ImGui::BeginTable("Memory", 2, flags)){

				ImGui::TableSetupColumn("Label",             ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn("Allocated Memory",  ImGuiTableColumnFlags_WidthStretch, 1.0f);

				ImGui::TableHeadersRow();

				int64_t sum = 0;
				for(auto allocation : CURuntime::allocations){
					
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text(allocation.label.c_str());

					ImGui::TableNextColumn();
					string strMemory = format(getSaneLocale(), "{:L}", allocation.size);
					alignRight(strMemory);
					ImGui::Text(strMemory.c_str());

					sum += allocation.size;
				}

				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("-----------------------");
					ImGui::TableNextColumn();
					ImGui::Text(" ");

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("Total");
					ImGui::TableNextColumn();
					string strTotal = format(getSaneLocale(), "{:L}", sum);
					alignRight(strTotal);
					ImGui::Text(strTotal.c_str());
				}

				ImGui::EndTable();
			}

			ImGui::Text("===============================");
			ImGui::Text("## VIRTUAL MEMORY ALLOCATIONS");
			ImGui::Text("===============================");

			if(ImGui::BeginTable("Memory", 2, flags)){

				ImGui::TableSetupColumn("Label",             ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn("allocated/comitted memory",  ImGuiTableColumnFlags_WidthStretch, 1.0f);

				ImGui::TableHeadersRow();

				int64_t sum = 0;
				for(auto allocation : CURuntime::allocations_virtual){
					
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text(allocation.label.c_str());

					ImGui::TableNextColumn();
					string strMemory = format(getSaneLocale(), "{:L}", allocation.memory->comitted);
					alignRight(strMemory);
					ImGui::Text(strMemory.c_str());

					sum += allocation.memory->comitted;
				}

				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("-----------------------");
					ImGui::TableNextColumn();
					ImGui::Text(" ");

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("Total");
					ImGui::TableNextColumn();
					string strTotal = format(getSaneLocale(), "{:L}", sum);
					alignRight(strTotal);
					ImGui::Text(strTotal.c_str());
				}

				ImGui::EndTable();
			}

			ImGui::Text("===============================");
			ImGui::Text("## OpenGL TEXTURE ALLOCATIONS");
			ImGui::Text("===============================");

			if(ImGui::BeginTable("OpenGL Textures", 2, flags)){

				ImGui::TableSetupColumn("Label",             ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn("allocated memory",  ImGuiTableColumnFlags_WidthStretch, 1.0f);

				ImGui::TableHeadersRow();

				int64_t sum = 0;
				for(auto texture : GLRenderer::textures){
					
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text(texture->label.c_str());

					int64_t size = 4 * texture->width * texture->height;

					ImGui::TableNextColumn();
					string strMemory = format(getSaneLocale(), "{:L}", size);
					alignRight(strMemory);
					ImGui::Text(strMemory.c_str());

					sum += size;
				}

				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("-----------------------");
					ImGui::TableNextColumn();
					ImGui::Text(" ");

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("Total");
					ImGui::TableNextColumn();
					string strTotal = format(getSaneLocale(), "{:L}", sum);
					alignRight(strTotal);
					ImGui::Text(strTotal.c_str());
				}

				ImGui::EndTable();
			}
		}

		settings.showMemoryInfos = open;

		ImGui::End();
	}

}

void makeTimings(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;

	if(settings.showTimingInfos){

		ImVec2 windowSize = {800, 700};
		ImGui::SetNextWindowPos({
			GLRenderer::width - windowSize.x - 10,
			(GLRenderer::height - windowSize.y) / 2, }, 
			ImGuiCond_Once);
		ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);

		static bool open = true;
		if(ImGui::Begin("Timings", &open)){

			static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
			
			if (ImGui::BeginTable("Timings", 3, flags)){

				// HEADER
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 4.0f);
				ImGui::TableSetupColumn("ms (mean | min | max)", ImGuiTableColumnFlags_WidthStretch, 2.0f);
				ImGui::TableSetupColumn("Calls/Frame", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableHeadersRow();

				auto timings = Runtime::timings;

				auto timeToColor = [](float duration){
					
					// https://colorbrewer2.org/#type=diverging&scheme=RdYlGn&n=10
					uint32_t color = 0xffffffff; 
					if(duration > 10.0)      {color = IM_COL32( 65,  0, 38, 255);}
					else if(duration > 5.0)  {color = IM_COL32(215, 48, 39, 255);}
					else if(duration > 1.0)  {color = IM_COL32(244,109, 67, 255);}
					else if(duration > 0.5)  {color = IM_COL32(253,174, 97, 255);}
					else if(duration > 0.1)  {color = IM_COL32(254,224,139, 255);}
					else if(duration > 0.0)  {color = IM_COL32(217,239,139, 255);}

					return color;
				};

				for(auto [label, list] : Runtime::timings.entries){

					float mean = Runtime::timings.getMean(label);
					float min = Runtime::timings.getMin(label);
					float max = Runtime::timings.getMax(label);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label.c_str());

					ImGui::TableSetColumnIndex(1);
					string strTime = format("{:6.3f} | {:6.3f} | {:6.3f}", mean, min, max);

					ImU32 color = timeToColor(mean); 
					ImGui::PushStyleColor(ImGuiCol_Text, color);
					ImGui::Text(strTime.c_str());
					ImGui::PopStyleColor();

					ImGui::TableSetColumnIndex(2);
					ImGui::Text(" ");

				}

				auto createTimeRows = [&](CudaModularProgram* program){
					for(auto& [label, list] : program->last_launch_durations){
						float duration = program->getAvgTiming(label);
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(label.c_str());

						ImGui::TableSetColumnIndex(1);
						string strTime = format("{:6.3f}", duration);
						// ImGui::TextUnformatted(strTime.c_str());

						
						ImU32 color = timeToColor(duration); 
						ImGui::PushStyleColor(ImGuiCol_Text, color);
						ImGui::Text(strTime.c_str());
						ImGui::PopStyleColor();

						ImGui::TableSetColumnIndex(2);
						string strLaunches = format("{}", program->launches_per_frame[label]);
						ImGui::Text(strLaunches.c_str());

					}
				};

				createTimeRows(editor->prog_jpeg);

				ImGui::EndTable();
			}
			
		}

		settings.showTimingInfos = open;

		ImGui::End();
	}
	Runtime::measureTimings = settings.showTimingInfos;

}

void SplatEditor::makeDevGUI(){
	makeKernels();
	makeMemory();
	makeTimings();
}