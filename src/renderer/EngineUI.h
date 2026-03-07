#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <functional>
#include "imgui_impl_vulkan.h"

struct EditorStats
{
	/** Seconds since last frame */
	float deltaTime = 0;
	/** 1.f / deltaTime, smoothed */
	float fps = 0;
};

/**
 * EngineUI owns the Dear ImGui Vulkan/GLFW backends and all editor overlay state.
 * Renderer constructs one instance, passing in the Vulkan handles it needs.
 */
class EngineUI
{
public:
	EngineUI(
		GLFWwindow* window,
		VkInstance            instance,
		VkPhysicalDevice      physicalDevice,
		VkDevice              device,
		uint32_t              graphicsFamily,
		VkQueue               graphicsQueue,
		VkRenderPass          renderPass,
		int                   maxFramesInFlight,
		std::function<void()> onReload
	);
	~EngineUI();

	/** Record ImGui draw commands into cmd. Must be called inside a render pass. */
	void Draw(VkCommandBuffer cmd, const EditorStats& stats);

	[[nodiscard]]
	bool IsWireframe() const
	{
		return ui.wireframe;
	}

	EngineUI(const EngineUI&) = delete;
	EngineUI& operator=(const EngineUI&) = delete;

private:
	GLFWwindow* window;
	VkDevice device;
	std::function<void()> onReload;

	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

	struct State
	{
		bool showStats = true;
		bool showAbout = false;
		bool wireframe = false;
	} ui;
};