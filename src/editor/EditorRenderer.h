#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include "renderer/VulkanBase.h"
#include "core/Game.h"
#include "core/World.h"
#include "EngineUI.h"

/**
 * Editor renderer.
 */
class Renderer : public VulkanBase
{
public:
	Renderer(
		GLFWwindow* window,
		std::function<void()> onReload,
		std::function<void()> onBuildAndReload,
		std::function<void()> onExport,
		std::function<void(const std::string&, const std::string&)> onNewProject,
		std::function<void(const std::string&)> onLoadProject,
		std::function<std::vector<EntityTypeInfo>()> onGetEntityTypes
	);
	~Renderer() override;

	void DrawFrame(const SpriteList& scene, const EditorStats& stats, World& world);

	[[nodiscard]]
	bool HasPendingWorldLoad() const;

	[[nodiscard]]
	std::string GetPendingWorldLoadPath() const;

	void ClearPendingWorldLoad() const;

	[[nodiscard]]
	PlayState GetPlayState() const;

	[[nodiscard]]
	float GetTimescale() const;

	[[nodiscard]]
	float GetFixedStep() const;
	bool ConsumeStepFrame() const;
	void NotifyWorldRestored() const;

private:
	bool wireframe = false;

	std::unique_ptr<EngineUI> engineUI;

	std::function<void()> onReload;
	std::function<void()> onBuildAndReload;
	std::function<void()> onExport;
	std::function<void(const std::string&, const std::string&)> onNewProject;
	std::function<void(const std::string&)> onLoadProject;
	std::function<std::vector<EntityTypeInfo>()> onGetEntityTypes;
};