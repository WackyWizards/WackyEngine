#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

/**
 * Owns the Vulkan objects that back a single uploaded texture.
 * Created and destroyed via VulkanBase::LoadTexture / DestroyTexture.
 */
struct Texture
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	int width = 0;
	int height = 0;

	[[nodiscard]]
	bool IsValid() const;
};