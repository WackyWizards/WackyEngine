/**
 * GLFW and Vulkan headers must come before windows.h so that APIENTRY is already
 * defined by the time windows.h is processed, preventing the C4005 redefinition warning.
 */
#define GLFW_INCLUDE_VULKAN
#include "EngineUI.h"
#include "core\World.h"
#include "core\Reflection.h"
#include "core\EntityRegistry.h"
#include "imgui_impl_glfw.h"
#include <numbers>
#include <stdexcept>
#include <map>
#include <array>
#include <algorithm>
#include <unordered_set>

 /**
  * Native OS file and folder dialogs (Windows only).
  *
  * windows.h is included here - after GLFW/Vulkan - so that APIENTRY is already
  * defined and windows.h silently skips its own definition, eliminating C4005.
  * glfw3native.h is intentionally avoided: GetActiveWindow() is sufficient for
  * dialog ownership and sidesteps the GLFWwindow/GLFWAPI forward-declaration issues.
  */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

  /**
   * Opens a native file picker dialog.
   * filter follows the OPENFILENAMEW double-NUL format,
   * e.g. L"JSON Files\0*.json\0All Files\0*.*\0"
   * Returns the selected path as UTF-8, or an empty string if cancelled.
   */
static std::string NativeOpenFile(const wchar_t* filter, const wchar_t* defExt)
{
	wchar_t buf[MAX_PATH] = {};

	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = defExt;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameW(&ofn))
	{
		return {};
	}

	int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, buf, -1, result.data(), len, nullptr, nullptr);
	return result;
}

/**
 * Opens a native save-file dialog.
 * Returns the chosen path as UTF-8, or an empty string if cancelled.
 */
static std::string NativeSaveFile(const wchar_t* filter, const wchar_t* defExt,
	const wchar_t* defaultName = nullptr)
{
	wchar_t buf[MAX_PATH] = {};

	if (defaultName)
	{
		wcsncpy_s(buf, defaultName, MAX_PATH - 1);
	}

	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = defExt;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (!GetSaveFileNameW(&ofn))
	{
		return {};
	}

	const int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, buf, -1, result.data(), len, nullptr, nullptr);
	return result;
}

/**
 * Opens a native folder browser using IFileOpenDialog (Vista+).
 * startPath sets the initial directory shown in the dialog.
 * Returns the selected folder path as UTF-8, or an empty string if cancelled.
 */
static std::string NativeBrowseFolder(const std::string& startPath = {})
{
	std::string result;

	/** CoInitializeEx failure is non-fatal - the dialog may still work. */
	static_cast<void>(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

	IFileOpenDialog* pfd = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
	{
		DWORD opts = 0;
		pfd->GetOptions(&opts);
		pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);

		if (!startPath.empty())
		{
			std::wstring wp(startPath.begin(), startPath.end());
			IShellItem* psi = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(wp.c_str(), nullptr, IID_PPV_ARGS(&psi))))
			{
				pfd->SetFolder(psi);
				psi->Release();
			}
		}

		if (SUCCEEDED(pfd->Show(GetActiveWindow())))
		{
			IShellItem* psi = nullptr;
			if (SUCCEEDED(pfd->GetResult(&psi)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					const int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
					result.resize(len - 1);
					WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
					CoTaskMemFree(path);
				}
				psi->Release();
			}
		}

		pfd->Release();
	}

	CoUninitialize();
	return result;
}

#else

  /**
   * Non-Windows stubs - callers treat an empty return value as cancellation.
   */
static std::string NativeOpenFile(const wchar_t*, const wchar_t*)
{
	return {};
}

static std::string NativeSaveFile(const wchar_t*, const wchar_t*, const wchar_t* = nullptr)
{
	return {};
}

static std::string NativeBrowseFolder(const std::string & = {})
{
	return {};
}

#endif

static void VkCheckUI(VkResult result, const char* msg)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(std::string(msg) + " failed");
	}
}

static constexpr float k_PI = std::numbers::pi_v<float>;

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
	std::function<void()> onExport,
	std::function<void(const std::string&, const std::string&)> onNewProject,
	std::function<void(const std::string&)> onLoadProject,
	std::function<std::vector<EntityTypeInfo>()> onGetEntityTypes
) : window{ window }, device{ device },
onReload{ std::move(onReload) },
onBuildAndReload{ std::move(onBuildAndReload) },
onExport{ std::move(onExport) },
onNewProject{ std::move(onNewProject) },
onLoadProject{ std::move(onLoadProject) },
onGetEntityTypes{ std::move(onGetEntityTypes) }
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

std::vector<EntityTypeInfo> EngineUI::GetAllEntityTypes() const
{
	/**
	 * Combine engine-built-in entities with game-specific entities from the DLL,
	 * deduplicating by type name so engine types always take priority.
	 *
	 * Root cause of duplicates: engine core files (Object.cpp etc.) are compiled
	 * into both the engine exe and the game DLL, giving each binary its own
	 * EntityRegistry singleton. Built-in types like SpinnerEntity therefore
	 * register in both registries and would appear twice without this guard.
	 *
	 * The proper fix is in CMakeLists.txt - engine source files should not be
	 * compiled into the game DLL. This deduplication acts as a safety net.
	 */
	std::vector<EntityTypeInfo> types;
	std::unordered_set<std::string> seen;

	const std::vector<EntityTypeInfo>& engineTypes = EntityRegistry::Get().GetAll();
	for (const EntityTypeInfo& info : engineTypes)
	{
		if (info.typeName)
		{
			types.push_back(info);
			seen.insert(info.typeName);
		}
	}

	/**
	 * Append game DLL entities, skipping any whose name already came
	 * from the engine registry.
	 */
	if (onGetEntityTypes)
	{
		const auto gameTypes = onGetEntityTypes();
		for (const EntityTypeInfo& info : gameTypes)
		{
			if (info.typeName && seen.find(info.typeName) == seen.end())
			{
				types.push_back(info);
				seen.insert(info.typeName);
			}
		}
	}

	return types;
}

Entity* EngineUI::DrawCreateEntityMenu(World& world) const
{
	/**
	 * Get combined entity types (engine + game).
	 */
	const std::vector<EntityTypeInfo> types = GetAllEntityTypes();

	Entity* created = nullptr;

	if (types.empty())
	{
		if (ImGui::MenuItem("Entity"))
		{
			created = world.CreateEntity("Entity");
		}
		return created;
	}

	std::map<std::string, std::vector<const EntityTypeInfo*>> byCategory;
	for (const EntityTypeInfo& info : types)
	{
		const std::string category = (info.category && info.category[0] != '\0') ? info.category : "General";
		byCategory[category].push_back(&info);
	}

	if (byCategory.size() == 1)
	{
		for (const EntityTypeInfo* info : byCategory.begin()->second)
		{
			if (!info->typeName || !info->factory)
			{
				continue;
			}

			if (ImGui::MenuItem(info->typeName))
			{
				created = world.SpawnEntity(info->factory(), info->typeName);
			}
		}

		return created;
	}

	for (const auto& [category, infos] : byCategory)
	{
		if (ImGui::BeginMenu(category.c_str()))
		{
			for (const EntityTypeInfo* info : infos)
			{
				if (!info->typeName || !info->factory)
				{
					continue;
				}

				if (ImGui::MenuItem(info->typeName))
				{
					created = world.SpawnEntity(info->factory(), info->typeName);
				}
			}

			ImGui::EndMenu();
		}
	}

	return created;
}

/**
 * Unicode stand-ins for play control icons.
 * TODO: Load FontAwesome for proper icon support.
 */
#define ICON_PLAY  "\xe2\x96\xb6"
#define ICON_PAUSE "\xe2\x8f\xb8"
#define ICON_STOP  "\xe2\x8f\xb9"

void EngineUI::DrawMenuBar(World& world)
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	/** File menu */
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Project..."))
		{
			ui.showNewProject = true;
		}

		if (ImGui::MenuItem("Open Project..."))
		{
			const auto path = NativeOpenFile(L"Project Files\0*.json\0All Files\0*.*\0", L"json");
			if (!path.empty() && onLoadProject)
			{
				onLoadProject(path);
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Build & Reload", "Ctrl+B"))
		{
			if (onBuildAndReload)
			{
				onBuildAndReload();
			}
		}

		if (ImGui::MenuItem("Reload DLL (no build)", "Ctrl+R"))
		{
			if (onReload)
			{
				onReload();
			}
		}


		if (ImGui::MenuItem("Export", "Ctrl+R"))
		{
			if (onExport)
			{
				onExport();
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Exit", "Alt+F4"))
		{
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}

		ImGui::EndMenu();
	}

	/** World menu */
	if (ImGui::BeginMenu("World"))
	{
		const bool inEditor = (ui.playState == PlayState::Editing);
		if (!inEditor)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::MenuItem("New World"))
		{
			world.Clear();
			world.name = "Untitled World";
			ui.selectedId.clear();
		}

		if (ImGui::MenuItem("Open World..."))
		{
			const auto path = NativeOpenFile(L"World Files\0*.json\0All Files\0*.*\0", L"json");
			if (!path.empty())
			{
				ui.pendingLoadWorldPath = path;
				ui.loadWorldRequested = true;
				ui.selectedId.clear();
			}
		}

		if (ImGui::MenuItem("Save World As..."))
		{
			const auto path = NativeSaveFile(L"World Files\0*.json\0All Files\0*.*\0", L"json");
			if (!path.empty())
			{
				world.SaveToJson(path);
			}
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("Create Entity"))
		{
			const Entity* entity = DrawCreateEntityMenu(world);
			if (entity)
			{
				ui.selectedId = entity->id;
			}
			ImGui::EndMenu();
		}

		if (!inEditor)
		{
			ImGui::EndDisabled();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("World Settings..."))
		{
			ui.showWorldSettings = true;
		}

		ImGui::EndMenu();
	}

	/** View menu */
	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Stats Overlay", "F1", &ui.showStats);
		ImGui::MenuItem("Hierarchy", nullptr, &ui.showHierarchy);
		ImGui::MenuItem("Properties", nullptr, &ui.showProperties);
		ImGui::MenuItem("Viewport", nullptr, &ui.showViewport);
		ImGui::Separator();

		if (ImGui::MenuItem("Wireframe", "F3", ui.wireframe))
		{
			ui.wireframe = !ui.wireframe;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Reset Camera"))
		{
			ui.vpPanX = ui.vpPanY = 0.f;
			ui.vpZoom = 64.f;
		}

		ImGui::EndMenu();
	}

	/** Help menu */
	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("About"))
		{
			ui.showAbout = true;
		}
		ImGui::EndMenu();
	}

	/** Play controls */
	{
		const bool editing = (ui.playState == PlayState::Editing);
		const bool playing = (ui.playState == PlayState::Playing);
		const bool paused = (ui.playState == PlayState::Paused);

		constexpr float btnW = 70.f;
		constexpr float sliderW = 90.f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;

		const float totalW = btnW * 3.f + btnW * 0.6f + sliderW + spacing * 5.f;
		const float avail = ImGui::GetContentRegionAvail().x;
		const float worldNameW = ImGui::CalcTextSize(world.name.c_str()).x + 16.f;
		const float centreOff = (avail - worldNameW - totalW) * 0.5f;

		if (centreOff > 0.f)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centreOff);
		}

		/** Play / Resume button */
		if (playing)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.68f, 0.20f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.44f, 0.12f, 1.f));
		}

		const char* playLabel = editing ? ICON_PLAY " Play" : (paused ? ICON_PLAY " Resume" : ICON_PLAY " Playing");
		if (ImGui::Button(playLabel, { btnW, 0.f }))
		{
			if (editing || paused)
			{
				ui.playState = PlayState::Playing;
			}
		}

		if (playing)
		{
			ImGui::PopStyleColor(3);
		}

		ImGui::SameLine();

		/** Pause button */
		if (!playing)
		{
			ImGui::BeginDisabled();
		}

		if (paused)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.46f, 0.10f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.58f, 0.13f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.38f, 0.08f, 1.f));
		}

		if (ImGui::Button(ICON_PAUSE " Pause", { btnW, 0.f }))
		{
			if (playing)
			{
				ui.playState = PlayState::Paused;
			}
		}

		if (paused)
		{
			ImGui::PopStyleColor(3);
		}

		if (!playing)
		{
			ImGui::EndDisabled();
		}

		ImGui::SameLine();

		/** Stop button */
		const bool stopEnabled = playing || paused;
		if (!stopEnabled)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button(ICON_STOP " Stop", { btnW, 0.f }))
		{
			ui.playState = PlayState::Editing;
		}

		if (!stopEnabled)
		{
			ImGui::EndDisabled();
		}

		ImGui::SameLine();

		/** Step Frame button - only meaningful when Paused */
		if (!paused)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button("\xe2\x8f\xad", { btnW * 0.6f, 0.f }))
		{
			ui.stepFrameRequested = true;
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Step one fixed tick forward (Paused only)");
		}

		if (!paused)
		{
			ImGui::EndDisabled();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		/** Inline timescale slider */
		ImGui::SetNextItemWidth(sliderW);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.20f, 0.24f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.40f, 0.70f, 1.00f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.82f, 1.00f, 1.f));

		if (ImGui::SliderFloat("##ts", &ui.timescale, 0.f, 1.f, "%.2fx"))
		{
			ui.timescale = std::max(0.f, ui.timescale);
		}

		ImGui::PopStyleColor(3);

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Timescale: %.2fx\nCtrl+click to type any value (e.g. 2.0 for double speed)", ui.timescale);
		}

		ImGui::SameLine();
		ImGui::TextDisabled("| %s", world.name.c_str());
	}

	ImGui::EndMainMenuBar();
}

void EngineUI::DrawHierarchyPanel(World& world)
{
	if (!ui.showHierarchy)
	{
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	const float menuBarH = ImGui::GetFrameHeight();
	constexpr float panelWidth = 260.f;
	const float panelHeight = io.DisplaySize.y - menuBarH;

	ImGui::SetNextWindowPos({ 0.f, menuBarH }, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ panelWidth, panelHeight }, ImGuiCond_Always);

	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

	if (!ImGui::Begin("Hierarchy", &ui.showHierarchy, flags))
	{
		ImGui::End();
		return;
	}

	const bool inEditor = ui.playState == PlayState::Editing;

	/** Read-only notice when not in editor mode */
	if (!inEditor)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.65f, 0.1f, 1.f));
		ImGui::TextWrapped(ui.playState == PlayState::Paused
			? ICON_PAUSE "  Paused - stop to edit"
			: ICON_PLAY  "  Playing - pause or stop to edit");
		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	/** Entity creation context menu (editor only) */
	if (inEditor)
	{
		if (ImGui::BeginPopupContextWindow("##hier_bg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::BeginMenu("Create Entity"))
			{
				const Entity* entity = DrawCreateEntityMenu(world);
				if (entity)
				{
					ui.selectedId = entity->id;
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
	}

	Entity* toDelete = nullptr;

	for (const auto& entityPtr : world.GetEntities())
	{
		Entity* entity = entityPtr.get();
		const bool sel = (entity->id == ui.selectedId);

		ImGuiTreeNodeFlags nodeFlags =
			ImGuiTreeNodeFlags_Leaf |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_NoTreePushOnOpen;

		if (sel)
		{
			nodeFlags |= ImGuiTreeNodeFlags_Selected;
		}

		if (ui.renamingEntity && sel && inEditor)
		{
			ImGui::SetNextItemWidth(-1.f);
			ImGui::PushID(entity->id.c_str());
			if (ImGui::InputText("##rename", ui.renameBuffer, sizeof(ui.renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				entity->name = ui.renameBuffer;
				ui.renamingEntity = false;
			}
			if (!ImGui::IsItemFocused())
			{
				ui.renamingEntity = false;
			}
			ImGui::PopID();
		}
		else
		{
			if (!entity->active)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.f));
			}

			ImGui::TreeNodeEx(entity->id.c_str(), nodeFlags, "%s", entity->name.c_str());
			const bool rowClicked = ImGui::IsItemClicked();

			if (!entity->active)
			{
				ImGui::PopStyleColor();
			}

			{
				const float availX = ImGui::GetContentRegionAvail().x;
				const float typeW = ImGui::CalcTextSize(entity->GetTypeName()).x + 4.f;
				ImGui::SameLine(availX - typeW);
				ImGui::TextDisabled("%s", entity->GetTypeName());
			}

			if (rowClicked)
			{
				ui.selectedId = entity->id;
			}

			if (inEditor)
			{
				ImGui::PushID(entity->id.c_str());
				if (ImGui::BeginPopupContextItem("##entity_ctx"))
				{
					if (ImGui::MenuItem("Rename"))
					{
						ui.selectedId = entity->id;
						strncpy_s(ui.renameBuffer, entity->name.c_str(), sizeof(ui.renameBuffer) - 1);
						ui.renamingEntity = true;
					}

					if (ImGui::MenuItem(entity->active ? "Deactivate" : "Activate"))
					{
						entity->active = !entity->active;
					}

					ImGui::Separator();

					if (ImGui::MenuItem("Duplicate"))
					{
						Entity* copy = nullptr;
						const auto allTypes = GetAllEntityTypes();
						for (const auto& info : allTypes)
						{
							if (info.typeName && strcmp(info.typeName, entity->GetTypeName()) == 0 && info.factory)
							{
								copy = world.SpawnEntity(info.factory(), entity->name + " (copy)");
								break;
							}
						}

						if (!copy)
						{
							copy = world.CreateEntity(entity->name + " (copy)");
						}

						copy->active = entity->active;
						copy->transform = entity->transform;
						ui.selectedId = copy->id;
					}

					ImGui::Separator();

					if (ImGui::MenuItem("Delete"))
					{
						toDelete = entity;
					}

					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
		}
	}

	if (inEditor && toDelete)
	{
		if (ui.selectedId == toDelete->id)
		{
			ui.selectedId.clear();
		}

		world.DestroyEntity(toDelete);
	}

	ImGui::End();
}

void EngineUI::DrawPropertiesPanel(World& world)
{
	if (!ui.showProperties)
	{
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	const float menuBarH = ImGui::GetFrameHeight();
	const float panelWidth = 300.f;
	const float panelHeight = io.DisplaySize.y - menuBarH;

	ImGui::SetNextWindowPos({ io.DisplaySize.x - panelWidth, menuBarH }, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ panelWidth, panelHeight }, ImGuiCond_Always);

	constexpr ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

	if (!ImGui::Begin("Properties", &ui.showProperties, flags))
	{
		ImGui::End();
		return;
	}

	Entity* selectedEntity = nullptr;
	for (const auto& entityPtr : world.GetEntities())
	{
		if (entityPtr->id == ui.selectedId)
		{
			selectedEntity = entityPtr.get();
			break;
		}
	}

	if (!selectedEntity)
	{
		ImGui::TextDisabled("(nothing selected)");
		ImGui::End();
		return;
	}

	const bool inEditor = (ui.playState == PlayState::Editing);

	/** Identity row */
	ImGui::TextDisabled("%s", selectedEntity->GetTypeName());
	ImGui::SameLine();

	char nameBuf[128];
	strncpy_s(nameBuf, selectedEntity->name.c_str(), sizeof(nameBuf) - 1);
	ImGui::SetNextItemWidth(-1.f);

	if (!inEditor)
	{
		ImGui::BeginDisabled();
	}

	if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
	{
		selectedEntity->name = nameBuf;
	}

	if (!inEditor)
	{
		ImGui::EndDisabled();
	}

	if (!inEditor)
	{
		ImGui::BeginDisabled();
	}

	ImGui::Checkbox("Active", &selectedEntity->active);

	if (!inEditor)
	{
		ImGui::EndDisabled();
	}

	ImGui::Spacing();
	ImGui::TextDisabled("ID: %s", selectedEntity->id.c_str());
	ImGui::Separator();

	/** Transform */
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (!inEditor)
		{
			ImGui::BeginDisabled();
		}

		float pos[2] = { selectedEntity->transform.position.x, selectedEntity->transform.position.y };
		ImGui::SetNextItemWidth(-1.f);
		if (ImGui::DragFloat2("Position", pos, 0.5f, 0.f, 0.f, "%.2f"))
		{
			selectedEntity->transform.position.x = pos[0];
			selectedEntity->transform.position.y = pos[1];
		}

		ImGui::SetNextItemWidth(-1.f);
		ImGui::DragFloat("Rotation °", &selectedEntity->transform.rotation.angle, 0.5f, -360.f, 360.f, "%.1f°");

		float scl[2] = { selectedEntity->transform.scale.x, selectedEntity->transform.scale.y };
		ImGui::SetNextItemWidth(-1.f);
		if (ImGui::DragFloat2("Scale", scl, 0.01f, 0.001f, 1000.f, "%.3f"))
		{
			selectedEntity->transform.scale.x = scl[0];
			selectedEntity->transform.scale.y = scl[1];
		}

		ImGui::Spacing();
		if (ImGui::Button("Reset Transform", { -1.f, 0.f }))
		{
			selectedEntity->transform.position = {};
			selectedEntity->transform.rotation = {};
			selectedEntity->transform.scale = { 1.f, 1.f };
		}

		if (!inEditor)
		{
			ImGui::EndDisabled();
		}
	}

	/** Reflected fields */
	const std::vector<Field>* fields = Reflection::GetFields(selectedEntity->GetTypeName());
	if (fields && !fields->empty())
	{
		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!inEditor)
			{
				ImGui::BeginDisabled();
			}

			for (const Field& field : *fields)
			{
				ImGui::SetNextItemWidth(-1.f);
				ImGui::TextDisabled("%s", field.name.c_str());
				switch (field.type)
				{
				case FieldType::Float:
				{
					float val = field.GetValue<float>(selectedEntity);
					if (ImGui::DragFloat(("##" + field.name).c_str(), &val, 0.01f, 0.f, 0.f, "%.2f"))
					{
						field.SetValue<float>(selectedEntity, val);
					}
					break;
				}
				case FieldType::Int:
				{
					int val = field.GetValue<int>(selectedEntity);
					if (ImGui::DragInt(("##" + field.name).c_str(), &val, 1))
					{
						field.SetValue<int>(selectedEntity, val);
					}
					break;
				}
				case FieldType::Bool:
				{
					bool val = field.GetValue<bool>(selectedEntity);
					if (ImGui::Checkbox(("##" + field.name).c_str(), &val))
					{
						field.SetValue<bool>(selectedEntity, val);
					}
					break;
				}
				case FieldType::String:
				{
					/**
					 * String field buffers are cached by entity ID + field name
					 * to persist edits across frames without losing intermediate state.
					 */
					static std::map<std::string, std::array<char, 256>> stringBuffers;
					const std::string key = std::string(selectedEntity->id) + "." + field.name;

					if (stringBuffers.find(key) == stringBuffers.end())
					{
						const std::string& str = field.GetValue<std::string>(selectedEntity);
						std::array<char, 256>& buf = stringBuffers[key];
						strncpy_s(buf.data(), buf.size(), str.c_str(), str.size());
						buf[255] = '\0';
					}

					std::array<char, 256>& buf = stringBuffers[key];
					if (ImGui::InputText(("##" + field.name).c_str(), buf.data(), buf.size()))
					{
						field.SetValue<std::string>(selectedEntity, std::string(buf.data()));
					}
					break;
				}
				}
			}

			if (!inEditor)
			{
				ImGui::EndDisabled();
			}
		}
	}

	ImGui::End();
}

static ImVec2 WorldToScreen(float wx, float wy, float cx, float cy, float panX, float panY, float zoom)
{
	return { cx + (wx - panX) * zoom, cy + (wy - panY) * zoom };
}

static void ScreenToWorld(float sx, float sy, float cx, float cy, float panX, float panY, float zoom, float& outWx, float& outWy)
{
	outWx = panX + (sx - cx) / zoom;
	outWy = panY + (sy - cy) / zoom;
}

void EngineUI::DrawViewportPanel(World& world)
{
	if (!ui.showViewport)
	{
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	const float menuBarH = ImGui::GetFrameHeight();
	const float leftW = ui.showHierarchy ? 260.f : 0.f;
	const float rightW = ui.showProperties ? 300.f : 0.f;
	const float vpX = leftW;
	const float vpY = menuBarH;
	const float vpW = std::max(10.f, io.DisplaySize.x - leftW - rightW);
	const float vpH = io.DisplaySize.y - menuBarH;

	ImGui::SetNextWindowPos({ vpX, vpY }, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ vpW, vpH }, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(1.f);

	constexpr ImGuiWindowFlags vpFlags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoTitleBar;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
	const bool open = ImGui::Begin("##viewport", nullptr, vpFlags);
	ImGui::PopStyleVar();

	if (!open)
	{
		ImGui::End();
		return;
	}

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 winPos = ImGui::GetWindowPos();
	ImVec2 winSize = ImGui::GetWindowSize();
	ImVec2 winEnd = { winPos.x + winSize.x, winPos.y + winSize.y };

	float& zoom = ui.vpZoom;
	float& panX = ui.vpPanX;
	float& panY = ui.vpPanY;

	const float cx = winPos.x + winSize.x * 0.5f;
	const float cy = winPos.y + winSize.y * 0.5f;

	auto W2S = [&](float wx, float wy) -> ImVec2
		{
			return WorldToScreen(wx, wy, cx, cy, panX, panY, zoom);
		};

	auto S2W = [&](float sx, float sy, float& wx, float& wy)
		{
			ScreenToWorld(sx, sy, cx, cy, panX, panY, zoom, wx, wy);
		};

	dl->PushClipRect(winPos, winEnd, true);

	/** Background - tinted based on play state */
	const bool playing = (ui.playState == PlayState::Playing);
	const bool paused = (ui.playState == PlayState::Paused);
	ImU32 bgCol = IM_COL32(28, 28, 32, 255);
	if (playing)
	{
		bgCol = IM_COL32(20, 28, 20, 255);
	}
	if (paused)
	{
		bgCol = IM_COL32(28, 26, 18, 255);
	}
	dl->AddRectFilled(winPos, winEnd, bgCol);

	/** Grid */
	{
		float step = 1.f;
		while (step * zoom < 20.f) { step *= 5.f; }
		while (step * zoom > 100.f) { step /= 5.f; }
		if (step < 0.001f) { step = 0.001f; }

		float wx0, wy0, wx1, wy1;
		S2W(winPos.x, winPos.y, wx0, wy0);
		S2W(winPos.x + winSize.x, winPos.y + winSize.y, wx1, wy1);

		const ImU32 gridCol = IM_COL32(55, 55, 60, 255);

		for (float wx = std::floorf(wx0 / step) * step; wx <= wx1 + step; wx += step)
		{
			const bool axis = (std::fabsf(wx) < step * 0.05f);
			dl->AddLine(W2S(wx, wy0), W2S(wx, wy1),
				axis ? IM_COL32(180, 60, 60, 200) : gridCol,
				axis ? 1.5f : 1.f);
		}

		for (float wy = std::floorf(wy0 / step) * step; wy <= wy1 + step; wy += step)
		{
			const bool axis = (std::fabsf(wy) < step * 0.05f);
			dl->AddLine(W2S(wx0, wy), W2S(wx1, wy),
				axis ? IM_COL32(60, 180, 60, 200) : gridCol,
				axis ? 1.5f : 1.f);
		}
	}

	/** Entities - two passes: unselected first, selected on top */
	{
		const auto& entities = world.GetEntities();

		for (int pass = 0; pass < 2; ++pass)
		{
			for (const auto& entityPtr : entities)
			{
				const Entity* entity = entityPtr.get();
				const bool sel = (entity->id == ui.selectedId);
				if ((pass == 0 && sel) || (pass == 1 && !sel))
				{
					continue;
				}

				const float px = entity->transform.position.x;
				const float py = entity->transform.position.y;
				const float sx = entity->transform.scale.x;
				const float sy = entity->transform.scale.y;
				const float rad = entity->transform.rotation.angle * k_PI / 180.f;
				const float cosA = std::cosf(rad);
				const float sinA = std::sinf(rad);
				const float hx = std::max(std::fabsf(sx) * 0.5f, 0.25f);
				const float hy = std::max(std::fabsf(sy) * 0.5f, 0.25f);

				const float lcx[4] = { -hx,  hx,  hx, -hx };
				const float lcy[4] = { -hy, -hy,  hy,  hy };
				ImVec2 sc[4];
				for (int i = 0; i < 4; ++i)
				{
					sc[i] = W2S(px + lcx[i] * cosA - lcy[i] * sinA,
						py + lcx[i] * sinA + lcy[i] * cosA);
				}

				ImU32 fillCol, borderCol;
				if (!entity->active)
				{
					fillCol = IM_COL32(55, 55, 55, 60);
					borderCol = IM_COL32(110, 110, 110, 140);
				}
				else if (sel)
				{
					fillCol = IM_COL32(70, 120, 200, 90);
					borderCol = IM_COL32(100, 180, 255, 255);
				}
				else
				{
					fillCol = IM_COL32(50, 90, 150, 55);
					borderCol = IM_COL32(160, 170, 185, 190);
				}

				dl->AddQuadFilled(sc[0], sc[1], sc[2], sc[3], fillCol);
				dl->AddQuad(sc[0], sc[1], sc[2], sc[3], borderCol, sel ? 2.f : 1.f);

				ImVec2 pivot = W2S(px, py);
				dl->AddCircleFilled(pivot, sel ? 4.f : 3.f, borderCol);

				if (sel)
				{
					const float lineLen = std::max(hx, 0.35f) * 1.1f;
					const float ex = px + cosA * lineLen;
					const float ey = py + sinA * lineLen;
					dl->AddLine(pivot, W2S(ex, ey), IM_COL32(255, 80, 80, 220), 1.5f);

					ImVec2 tip = W2S(ex, ey);
					const float px2 = -sinA * 0.15f;
					const float py2 = cosA * 0.15f;
					dl->AddTriangleFilled(tip,
						W2S(ex - cosA * 0.18f - px2, ey - sinA * 0.18f - py2),
						W2S(ex - cosA * 0.18f + px2, ey - sinA * 0.18f + py2),
						IM_COL32(255, 80, 80, 220));
				}

				if (zoom >= 24.f)
				{
					ImVec2 lp = { pivot.x + 6.f, pivot.y - ImGui::GetFontSize() - 2.f };
					lp.x = std::clamp(lp.x, winPos.x + 2.f, winEnd.x - 2.f);
					lp.y = std::clamp(lp.y, winPos.y + 2.f, winEnd.y - 2.f);
					dl->AddText(lp,
						entity->active ? IM_COL32(220, 225, 235, 210) : IM_COL32(130, 130, 130, 160),
						entity->name.c_str());
				}
			}
		}
	}

	/** Axis indicator */
	{
		const ImVec2 origin = { winPos.x + 28.f, winPos.y + winSize.y - 28.f };
		const float len = 20.f;
		dl->AddLine(origin, { origin.x + len, origin.y }, IM_COL32(200, 60, 60, 220), 1.5f);
		dl->AddText({ origin.x + len + 3.f, origin.y - 7.f }, IM_COL32(200, 60, 60, 220), "X");
		dl->AddLine(origin, { origin.x, origin.y - len }, IM_COL32(60, 200, 60, 220), 1.5f);
		dl->AddText({ origin.x - 12.f, origin.y - len - 14.f }, IM_COL32(60, 200, 60, 220), "Y");
	}

	/** Zoom readout */
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.0f%%", zoom / 64.f * 100.f);
		const float tw = ImGui::CalcTextSize(buf).x;
		dl->AddText({ winEnd.x - tw - 8.f, winEnd.y - ImGui::GetFontSize() - 5.f },
			IM_COL32(160, 160, 170, 180), buf);
	}

	/** Play mode overlay badge */
	if (playing || paused)
	{
		const char* badge = playing ? ICON_PLAY "  PLAYING" : ICON_PAUSE "  PAUSED";
		const ImU32 badgeCol = playing ? IM_COL32(80, 200, 80, 200) : IM_COL32(220, 180, 40, 200);
		const float tw = ImGui::CalcTextSize(badge).x;
		dl->AddText({ winPos.x + winSize.x * 0.5f - tw * 0.5f, winPos.y + 44.f }, badgeCol, badge);
	}

	dl->PopClipRect();

	/** Invisible interaction button */
	ImGui::SetCursorScreenPos(winPos);
	ImGui::InvisibleButton("##vp_interact", winSize,
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonMiddle |
		ImGuiButtonFlags_MouseButtonRight);

	const bool vpHovered = ImGui::IsItemHovered();
	const bool vpActive = ImGui::IsItemActive();
	ImVec2 mp = io.MousePos;
	const bool inEditor = (ui.playState == PlayState::Editing);

	/** Zoom */
	if (vpHovered && io.MouseWheel != 0.f)
	{
		float mwx, mwy;
		S2W(mp.x, mp.y, mwx, mwy);
		const float factor = (io.MouseWheel > 0.f) ? 1.15f : (1.f / 1.15f);
		zoom = std::clamp(zoom * factor, 4.f, 2048.f);
		panX = mwx - (mp.x - cx) / zoom;
		panY = mwy - (mp.y - cy) / zoom;
	}

	/** Pan */
	const bool panMouse = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));

	if ((vpHovered || vpActive) && panMouse)
	{
		ui.vpPanning = true;
		panX -= io.MouseDelta.x / zoom;
		panY -= io.MouseDelta.y / zoom;
	}
	else if (!panMouse)
	{
		ui.vpPanning = false;
	}

	/** Select and drag */
	const bool plainLeft = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt;

	if (vpHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
	{
		float mwx, mwy;
		S2W(mp.x, mp.y, mwx, mwy);
		ui.vpDragging = false;
		Entity* hit = nullptr;

		const auto& entities = world.GetEntities();
		for (int i = static_cast<int>(entities.size()) - 1; i >= 0; --i)
		{
			Entity* entity = entities[i].get();
			const float dx = mwx - entity->transform.position.x;
			const float dy = mwy - entity->transform.position.y;
			const float ang = -entity->transform.rotation.angle * k_PI / 180.f;
			const float cA = std::cosf(ang);
			const float sA = std::sinf(ang);
			const float lx = dx * cA - dy * sA;
			const float ly = dx * sA + dy * cA;
			const float hsx = std::max(std::fabsf(entity->transform.scale.x) * 0.5f, 0.25f);
			const float hsy = std::max(std::fabsf(entity->transform.scale.y) * 0.5f, 0.25f);

			if (std::fabsf(lx) <= hsx && std::fabsf(ly) <= hsy)
			{
				hit = entity;
				break;
			}
		}

		if (hit)
		{
			ui.selectedId = hit->id;
			if (inEditor)
			{
				ui.vpDragging = true;
				ui.vpDragOffX = mwx - hit->transform.position.x;
				ui.vpDragOffY = mwy - hit->transform.position.y;
			}
		}
		else
		{
			ui.selectedId = {};
			ui.vpDragging = false;
		}
	}

	if (vpActive && ui.vpDragging && inEditor && plainLeft && !ui.selectedId.empty())
	{
		float mwx, mwy;
		S2W(mp.x, mp.y, mwx, mwy);
		for (const auto& ePtr : world.GetEntities())
		{
			if (ePtr->id == ui.selectedId)
			{
				ePtr->transform.position.x = mwx - ui.vpDragOffX;
				ePtr->transform.position.y = mwy - ui.vpDragOffY;
				break;
			}
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		ui.vpDragging = false;
	}

	/** Keyboard shortcuts */
	if (vpHovered)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_F))
		{
			if (!ui.selectedId.empty())
			{
				for (const auto& ePtr : world.GetEntities())
				{
					if (ePtr->id == ui.selectedId)
					{
						panX = ePtr->transform.position.x;
						panY = ePtr->transform.position.y;
						break;
					}
				}
			}
			else
			{
				panX = panY = 0.f;
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Home))
		{
			panX = panY = 0.f;
			zoom = 64.f;
		}
	}

	ImGui::End();
}

void EngineUI::Draw(const VkCommandBuffer cmd, const EditorStats& stats, World& world)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	DrawMenuBar(world);
	DrawViewportPanel(world);
	DrawHierarchyPanel(world);
	DrawPropertiesPanel(world);

	/** Stats overlay */
	if (ui.showStats)
	{
		const bool playing = ui.playState == PlayState::Playing;
		const bool paused = ui.playState == PlayState::Paused;
		const float menuBarH = ImGui::GetFrameHeight();
		const float hierarchyW = ui.showHierarchy ? 260.f : 0.f;
		const float h = playing || paused ? 110.f : 50.f;

		ImGui::SetNextWindowPos({ hierarchyW + 10.f, menuBarH + 6.f }, ImGuiCond_Always);
		ImGui::SetNextWindowSize({ 230.f, h }, ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.60f);

		constexpr ImGuiWindowFlags ov =
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

		ImGui::Begin("##stats", nullptr, ov);
		ImGui::Text("FPS   : %.1f", stats.fps);
		ImGui::Text("Delta : %.2f ms", stats.deltaTime * 1000.f);

		if (playing || paused)
		{
			ImGui::Separator();
			if (stats.timescale != 1.f)
			{
				ImGui::PushStyleColor(ImGuiCol_Text,
					stats.timescale < 1.f
					? ImVec4(1.f, 0.8f, 0.3f, 1.f)
					: ImVec4(0.4f, 1.f, 0.5f, 1.f));
			}

			ImGui::Text("Scale : %.2fx", stats.timescale);

			if (stats.timescale != 1.f)
			{
				ImGui::PopStyleColor();
			}

			const float fixedHz = stats.fixedStep > 0.f ? (1.f / stats.fixedStep) : 0.f;
			ImGui::Text("Fixed : %.0f Hz  (%d tick%s)",
				fixedHz, stats.fixedTicks, stats.fixedTicks == 1 ? "" : "s");
		}

		ImGui::End();
	}

	/** World Settings modal */
	if (ui.showWorldSettings)
	{
		ImGui::OpenPopup("World Settings##popup");
		ui.showWorldSettings = false;
	}

	if (ImGui::BeginPopupModal("World Settings##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::SeparatorText("World");
		char wnameBuf[128];
		strncpy_s(wnameBuf, world.name.c_str(), sizeof(wnameBuf) - 1);
		ImGui::SetNextItemWidth(240.f);
		if (ImGui::InputText("Name##wname", wnameBuf, sizeof(wnameBuf)))
		{
			world.name = wnameBuf;
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Simulation");

		ImGui::SetNextItemWidth(240.f);
		ImGui::SliderFloat("Slow motion (0-1)", &ui.timescale, 0.f, 1.f, "%.3f");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Ctrl+click to type any value including > 1");
		}

		ImGui::SetNextItemWidth(240.f);
		ImGui::DragFloat("Timescale override", &ui.timescale, 0.05f, 0.f, 0.f, "%.2fx");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Drag or Ctrl+click - no upper limit.\n"
				"< 1 = slow motion   1 = normal   > 1 = fast forward");
		}
		ui.timescale = std::max(0.f, ui.timescale);

		ImGui::Spacing();
		ImGui::TextDisabled("Presets:");
		struct { const char* label; float val; } presets[] = {
			{"0x",0.f},{"0.1x",0.1f},{"0.25x",0.25f},{"0.5x",0.5f},
			{"1x",1.f},{"2x",2.f},{"5x",5.f},{"10x",10.f}
		};
		for (auto& p : presets)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton(p.label))
			{
				ui.timescale = p.val;
			}
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Fixed Update");

		float fixedHz = ui.fixedStep > 0.f ? (1.f / ui.fixedStep) : 0.f;
		ImGui::SetNextItemWidth(140.f);
		if (ImGui::DragFloat("Hz##fixedhz", &fixedHz, 1.f, 1.f, 500.f, "%.0f Hz"))
		{
			if (fixedHz > 0.f)
			{
				ui.fixedStep = 1.f / fixedHz;
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.f);
		ImGui::DragFloat("Step##fixedstep", &ui.fixedStep, 0.001f, 0.001f, 1.f, "%.4f s");
		ui.fixedStep = std::max(0.001f, ui.fixedStep);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Fixed tick interval in seconds.\n"
				"Changing Hz or Step updates the other automatically.");
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Presets:");
		struct { const char* label; float hz; } hzPresets[] = {
			{"20Hz",20.f},{"30Hz",30.f},{"50Hz",50.f},{"60Hz",60.f},{"120Hz",120.f}
		};
		for (auto& p : hzPresets)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton(p.label))
			{
				ui.fixedStep = 1.f / p.hz;
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		if (ImGui::Button("Close", { 120.f, 0.f }))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	/** New Project modal */
	if (ui.showNewProject)
	{
		ImGui::OpenPopup("New Project##popup");
		ui.showNewProject = false;
	}

	if (ImGui::BeginPopupModal("New Project##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		/** Directory row: read-only display + native browse button */
		ImGui::SetNextItemWidth(300.f);
		ImGui::InputText("##projdir", ui.newProjDir, sizeof(ui.newProjDir), ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();

		if (ImGui::Button("Browse..."))
		{
			const auto folder = NativeBrowseFolder(ui.newProjDir);
			if (!folder.empty())
			{
				strncpy_s(ui.newProjDir, folder.c_str(), sizeof(ui.newProjDir) - 1);
			}
		}

		ImGui::SameLine();
		ImGui::TextUnformatted("Parent Directory");

		ImGui::SetNextItemWidth(300.f);
		ImGui::InputText("Project Name", ui.newProjName, sizeof(ui.newProjName));

		ImGui::Spacing();

		const bool canCreate = ui.newProjName[0] != '\0' && ui.newProjDir[0] != '\0';
		if (!canCreate)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button("Create", { 120.f, 0.f }))
		{
			if (onNewProject)
			{
				onNewProject(ui.newProjDir, ui.newProjName);
			}
			ImGui::CloseCurrentPopup();
		}

		if (!canCreate)
		{
			ImGui::EndDisabled();
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", { 80.f, 0.f }))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	 /** About modal */
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
		if (ImGui::Button("Close", { 120.f, 0.f }))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}