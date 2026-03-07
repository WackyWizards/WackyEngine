#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "core\Game.h"
#include "EngineUI.h"
#include <memory>
#include <functional>

/**
 * Renderer owns the entire Vulkan context.
 */
class Renderer
{
public:
	Renderer(GLFWwindow* window, std::function<void()> onReload);
	~Renderer();

	void DrawFrame(const SpriteList& scene, const EditorStats& stats);
	void WaitIdle() const;
private:
	/** @name Core handles */
	///@{
	GLFWwindow* window;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkSurfaceKHR surface;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	uint32_t graphicsFamily = 0;
	uint32_t presentFamily = 0;
	///@}

	/** @name Swapchain */
	///@{
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D swapchainExtent = {};
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	std::vector<VkFramebuffer> framebuffers;
	///@}

	/** @name Pipeline */
	///@{
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline graphicsPipeline = VK_NULL_HANDLE;
	///@}

	/** @name Command recording */
	///@{
	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> commandBuffers;
	///@}

	/** @name Sync primitives */
	///@{
	static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
	/* One per frame in flight, indexed by currentFrame */
	std::vector<VkSemaphore> imageAvailableSemaphores;
	/* One per swapchain image, indexed by imageIndex, safe to reuse only once that image is re-acquired */
	std::vector<VkSemaphore> renderFinishedSemaphores;
	/* One per frame in flight */
	std::vector<VkFence> inFlightFences;
	/* Cycles 0..MAX_FRAMES_IN_FLIGHT-1 */
	uint32_t currentFrame = 0;
	///@}

	/** @name State */
	///@{
	bool framebufferResized = false;
	///@}

	/** @name Sprite SSBO
	 *
	 *  One host-visible, persistently-mapped storage buffer per frame in flight.
	 *  Each frame the CPU memcpy's the current SpriteList into the buffer for
	 *  that frame slot, then the GPU reads it via the matching descriptor set.
	 *
	 *  Shader side (HLSL / Slang, set=0 binding=0):
	 *    struct Sprite     { float x, y, halfW, halfH, r, g, b; };
	 *    struct SpriteList { Sprite sprites[MAX_SPRITES]; uint count; };
	 *    [[vk::binding(0,0)]] StructuredBuffer<SpriteList> spriteData;
	 *
	 *  Then index with: SpriteList list = spriteData[0];
	 */
	 ///@{
	VkDescriptorSetLayout spriteSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool spriteDescriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> spriteDescriptorSets; // [MAX_FRAMES_IN_FLIGHT]
	std::vector<VkBuffer> spriteBuffers; // [MAX_FRAMES_IN_FLIGHT]
	std::vector<VkDeviceMemory> spriteBufferMemory; // [MAX_FRAMES_IN_FLIGHT]
	std::vector<void*> spriteBufferMapped; // [MAX_FRAMES_IN_FLIGHT] - persistently mapped

	void CreateSpriteSetLayout();
	void CreateSpriteBuffers();
	void CreateSpriteDescriptorPool();
	void CreateSpriteDescriptorSets();
	/* Returns the index of the first memory type that satisfies both typeFilter and props. */
	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
	///@}

	/** @name Editor UI */
	///@{
	/** Tracks the last-committed wireframe state so Renderer knows when to rebuild the pipeline. */
	bool wireframe = false;
	std::unique_ptr<EngineUI> engineUI;
	///@}

	/** @name Setup */
	///@{
	void CreateInstance();
	void CreateDebugMessenger();
	void CreateSurface();
	void PickPhysicalDevice();
	void CreateLogicalDevice();
	void CreateCommandPool();
	void CreateSyncObjects();
	///@}

	/** @name Swapchain setup */
	///@{
	void CreateSwapchain();
	void CreateImageViews();
	void CreateRenderPass();
	void CreateGraphicsPipeline();
	void CreateFramebuffers();
	void CreateCommandBuffers();
	/* Tears down swapchain resources in reverse order, ready for recreation. */
	void CleanupSwapchain();
	/* Tears down and rebuilds everything swapchain-dependent. */
	void RecreateSwapchain();
	///@}

	/** @name Helpers */
	///@{
	/* Load a compiled shader file (.spv from glslc, dxc, or slangc). */
	static std::vector<char> ReadFile(const std::string& path);

	[[nodiscard]]
	VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

	/* Throws a runtime_error with the Vulkan result code name included. */
	static void VkCheck(VkResult result, const char* msg);
	/* Validation layer callback - prints to cout so errors appear in VS output. */
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData);
	///@}
	/* GLFW calls this when the window is resized. Sets framebufferResized = true. */
	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
};