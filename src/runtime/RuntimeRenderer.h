#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "renderer/VulkanBase.h"
#include "core/Game.h"

/**
 * Export / runtime renderer.
 * Extends VulkanBase with no validation layers, no wireframe, and no ImGui.
 * DrawFrame only submits sprites and presents.
 */
class RuntimeRenderer : public VulkanBase
{
public:
    explicit RuntimeRenderer(GLFWwindow* window);
    ~RuntimeRenderer() override = default;

    void DrawFrame(const SpriteList& scene);

    RuntimeRenderer(const RuntimeRenderer&) = delete;
    RuntimeRenderer& operator=(const RuntimeRenderer&) = delete;
};