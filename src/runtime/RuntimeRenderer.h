#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "core/Game.h"

/**
 * Vulkan renderer for export builds.
 * All Vulkan setup is identical to Renderer.cpp.
 * Differences: no EngineUI, no ImGui, no wireframe, no validation layers,
 * DrawFrame only submits sprites and presents.
 */
class RuntimeRenderer
{
public:
	explicit RuntimeRenderer(GLFWwindow* window);
	~RuntimeRenderer();

	void DrawFrame(const SpriteList& scene);
	void WaitIdle() const;

	RuntimeRenderer(const RuntimeRenderer&) = delete;
	RuntimeRenderer& operator=(const RuntimeRenderer&) = delete;

private:

	GLFWwindow* window;
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	uint32_t graphicsFamily = 0;
	uint32_t presentFamily = 0;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D swapchainExtent = {};
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	std::vector<VkFramebuffer> framebuffers;

	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline graphicsPipeline = VK_NULL_HANDLE;

	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> commandBuffers;

	static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;
	uint32_t currentFrame = 0;

	bool framebufferResized = false;

	VkDescriptorSetLayout spriteSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool spriteDescriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> spriteDescriptorSets;
	std::vector<VkBuffer> spriteBuffers;
	std::vector<VkDeviceMemory> spriteBufferMemory;
	std::vector<void*> spriteBufferMapped;

	void CreateInstance();
	void CreateSurface();
	void PickPhysicalDevice();
	void CreateLogicalDevice();
	void CreateCommandPool();
	void CreateSyncObjects();
	void CreateSwapchain();
	void CreateImageViews();
	void CreateRenderPass();
	void CreateGraphicsPipeline();
	void CreateFramebuffers();
	void CreateCommandBuffers();
	void CleanupSwapchain();
	bool RecreateSwapchain();

	void CreateSpriteSetLayout();
	void CreateSpriteBuffers();
	void CreateSpriteDescriptorPool();
	void CreateSpriteDescriptorSets();

	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

	static std::vector<char> ReadFile(const std::string& path);

	[[nodiscard]]
	VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

	static void VkCheck(VkResult result, const char* msg);
	static void FramebufferResizeCallback(GLFWwindow* window, int, int);
};