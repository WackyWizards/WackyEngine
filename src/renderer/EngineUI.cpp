#define GLFW_INCLUDE_VULKAN
#include "EngineUI.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <stdexcept>

static void VkCheckUI(VkResult result, const char* msg)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(std::string(msg) + " failed");
	}
}

EngineUI::EngineUI(
	GLFWwindow* window,
	const VkInstance instance,
	const VkPhysicalDevice physicalDevice,
	const VkDevice device,
	const uint32_t graphicsFamily,
	const VkQueue graphicsQueue,
	const VkRenderPass renderPass,
	const int maxFramesInFlight,
	std::function<void()> onReload,
	std::function<void()> onBuildAndReload,
	std::function<void(const std::string&, const std::string&)> onNewProject,
	std::function<void(const std::string&)> onLoadProject
) : window(window), device(device), onReload(std::move(onReload)), onBuildAndReload(std::move(onBuildAndReload)), onNewProject(std::move(onNewProject)), onLoadProject(std::move(onLoadProject))
{
	constexpr VkDescriptorPoolSize poolSizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
	};

	VkDescriptorPoolCreateInfo poolCI{};
	poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolCI.maxSets = 16;
	poolCI.poolSizeCount = 1;
	poolCI.pPoolSizes = poolSizes;
	VkCheckUI(vkCreateDescriptorPool(device, &poolCI, nullptr, &descriptorPool), "vkCreateDescriptorPool imgui");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.ApiVersion = VK_API_VERSION_1_3;
	initInfo.Instance = instance;
	initInfo.PhysicalDevice = physicalDevice;
	initInfo.Device = device;
	initInfo.QueueFamily = graphicsFamily;
	initInfo.Queue = graphicsQueue;
	initInfo.DescriptorPool = descriptorPool;
	initInfo.MinImageCount = maxFramesInFlight;
	initInfo.ImageCount = maxFramesInFlight;
	initInfo.PipelineInfoMain.RenderPass = renderPass;
	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	ImGui_ImplVulkan_Init(&initInfo);
}

EngineUI::~EngineUI()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
}

void EngineUI::Draw(const VkCommandBuffer cmd, const EditorStats& stats)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Main menu bar
	if (ImGui::BeginMainMenuBar())
	{
		// --- File ---
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project..."))
			{
				ui.showNewProject = true;
			}

			if (ImGui::MenuItem("Open Project..."))
			{
				ui.showLoadProject = true;
			}

			ImGui::Separator();

			// Build game DLL via cmake, then hot-swap it into the running engine.
			if (ImGui::MenuItem("Build & Reload", "Ctrl+B"))
			{
				if (onBuildAndReload) onBuildAndReload();
			}

			// Reload without building (assumes the .dll on disk is already fresh).
			if (ImGui::MenuItem("Reload DLL (no build)", "Ctrl+R"))
			{
				if (onReload) onReload();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				glfwSetWindowShouldClose(window, GLFW_TRUE);
			}

			ImGui::EndMenu();
		}

		// View
		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Stats Overlay", "F1", &ui.showStats);

			ImGui::Separator();

			if (ImGui::MenuItem("Wireframe", "F3", ui.wireframe))
			{
				ui.wireframe = !ui.wireframe;
			}

			ImGui::EndMenu();
		}

		// Help 
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About"))
			{
				ui.showAbout = true;
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	// Stats overlay
	if (ui.showStats)
	{
		const float menuBarHeight = ImGui::GetFrameHeight();
		ImGui::SetNextWindowPos(ImVec2(10.f, menuBarHeight + 6.f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(210.f, 68.f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		constexpr ImGuiWindowFlags overlayFlags =
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
		ImGui::Begin("##stats", nullptr, overlayFlags);
		ImGui::Text("FPS   : %.1f", stats.fps);
		ImGui::Text("Delta : %.2f ms", stats.deltaTime * 1000.f);
		ImGui::End();
	}

	// New Project modal
	if (ui.showNewProject)
	{
		ImGui::OpenPopup("New Project##popup");
		ui.showNewProject = false;
	}
	if (ImGui::BeginPopupModal("New Project##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Parent Directory", ui.newProjDir, sizeof(ui.newProjDir));
		ImGui::InputText("Project Name", ui.newProjName, sizeof(ui.newProjName));
		ImGui::Spacing();
		if (ImGui::Button("Create", ImVec2(120.f, 0.f)))
		{
			if (onNewProject) onNewProject(ui.newProjDir, ui.newProjName);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80.f, 0.f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Open Project modal
	if (ui.showLoadProject)
	{
		ImGui::OpenPopup("Open Project##popup");
		ui.showLoadProject = false;
	}
	if (ImGui::BeginPopupModal("Open Project##popup", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText(".json path", ui.loadProjPath, sizeof(ui.loadProjPath));
		if (ImGui::Button("Open", ImVec2(120.f, 0.f)))
		{
			if (onLoadProject)
			{
				onLoadProject(ui.loadProjPath);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80.f, 0.f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// About popup
	if (ui.showAbout)
	{
		ImGui::OpenPopup("About##popup");
		ui.showAbout = false;
	}
	if (ImGui::BeginPopupModal("About##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("WackyEngine");
		ImGui::Separator();
		ImGui::Text("In-Development Game Engine");
		ImGui::Spacing();
		if (ImGui::Button("Close", ImVec2(120.f, 0.f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}