#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <vector>
#include "imgui_impl_vulkan.h"

class World;
class Entity;
struct EntityTypeInfo;

struct EditorStats
{
	/** Raw frame delta in seconds */
	float deltaTime = 0.f;
	/** Smoothed FPS */
	float fps = 0.f;
	/** Current timescale (for display) */
	float timescale = 1.f;
	/** Current fixed step in seconds (for display) */
	float fixedStep = 0.02f;
	/** Fixed ticks fired last frame (for display) */
	int fixedTicks = 0;
};

/** Editor simulation state - polled by Engine each frame. */
enum class PlayState
{
	/** Normal editor mode: entities are editable, game is not running. */
	Editing,
	/** Game loop is running at full (timescale-adjusted) speed. */
	Playing,
	/** Game loop is suspended; world is live but getDelta() returns 0. */
	Paused
};

class EngineUI
{
public:
	EngineUI(
		GLFWwindow* window,
		VkInstance instance,
		VkPhysicalDevice physicalDevice,
		VkDevice device,
		uint32_t graphicsFamily,
		VkQueue graphicsQueue,
		VkRenderPass renderPass,
		int maxFramesInFlight,
		std::function<void()> onReload,
		std::function<void()> onBuildAndReload,
		std::function<void()> onExport,
		std::function<void(const std::string&, const std::string&)> onNewProject,
		std::function<void(const std::string&)> onLoadProject,
		std::function<std::vector<EntityTypeInfo>()> onGetEntityTypes
	);
	~EngineUI();

	void Draw(VkCommandBuffer cmd, const EditorStats& stats, World& world);

	[[nodiscard]]
	bool IsWireframe() const
	{
		return ui.wireframe;
	}

	[[nodiscard]]
	PlayState GetPlayState() const
	{
		return ui.playState;
	}

	[[nodiscard]]
	float GetTimescale() const
	{
		return ui.timescale;
	}

	[[nodiscard]]
	float GetFixedStep() const
	{
		return ui.fixedStep;
	}

	/**
	 * Returns true (and clears the flag) when the user clicked Step Frame.
	 * Consumed by Engine to fire one fixed tick while Paused.
	 */
	bool ConsumeStepFrame()
	{
		if (ui.stepFrameRequested)
		{
			ui.stepFrameRequested = false;
			return true;
		}

		return false;
	}

	void NotifyWorldRestored() { ui.selectedId.clear(); }

	[[nodiscard]]
	bool HasPendingWorldLoad() const
	{
		return ui.loadWorldRequested;
	}

	[[nodiscard]]
	const std::string& PendingWorldLoadPath() const
	{
		return ui.pendingLoadWorldPath;
	}

	void ClearPendingWorldLoad()
	{
		ui.loadWorldRequested = false;
		ui.pendingLoadWorldPath.clear();
	}

	EngineUI(const EngineUI&) = delete;
	EngineUI& operator=(const EngineUI&) = delete;

private:
	GLFWwindow* window;
	VkDevice device;
	std::function<void()> onReload;
	std::function<void()> onBuildAndReload;
	std::function<void()> onExport;
	std::function<void(const std::string&, const std::string&)> onNewProject;
	std::function<void(const std::string&)> onLoadProject;
	std::function<std::vector<EntityTypeInfo>()> onGetEntityTypes;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

	struct State
	{
		bool showStats = true;
		bool showAbout = false;
		bool showHierarchy = true;
		bool showProperties = true;
		bool showViewport = true;
		bool wireframe = false;

		PlayState playState = PlayState::Editing;

		float timescale = 1.f;
		/** Seconds per fixed tick (50 Hz default) */
		float fixedStep = 0.02f;
		/** Consume one fixed tick while Paused */
		bool stepFrameRequested = false;

		bool showWorldSettings = false;

		// "New Project" still needs a name field; directory is picked via native browser.
		bool showNewProject = false;
		char newProjDir[512] = "C:/Projects";
		char newProjName[64] = "MyGame";
		bool loadWorldRequested = false;
		std::string pendingLoadWorldPath;

		// hierarchy
		std::string selectedId;
		bool renamingEntity = false;
		char renameBuffer[128] = "";

		// 2-D viewport camera
		float vpZoom = 64.f;   ///< Pixels per world unit. 64 == 100 %.
		float vpPanX = 0.f;    ///< World position at viewport centre.
		float vpPanY = 0.f;

		bool vpPanning = false;
		bool vpDragging = false;
		float vpDragOffX = 0.f;
		float vpDragOffY = 0.f;
	} ui;

	void DrawHierarchyPanel(World& world);
	void DrawPropertiesPanel(World& world);
	void DrawViewportPanel(World& world);
	void DrawMenuBar(World& world);
	Entity* DrawCreateEntityMenu(World& world) const;

	/**
	 * Gets all available entity types, combining engine-built-in types
	 * with game DLL types. Ensures base Entity is always available.
	 */
	std::vector<EntityTypeInfo> GetAllEntityTypes() const;
};