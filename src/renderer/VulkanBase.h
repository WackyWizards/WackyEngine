#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "core/Game.h"

/**
 * Shared Vulkan base class.
 * Owns all device, swapchain, sprite SSBO, and pipeline state that is
 * identical between the editor Renderer and the export RuntimeRenderer.
 */
class VulkanBase
{
public:
	explicit VulkanBase(GLFWwindow* window, bool validation = false, bool wireframe = false);
	virtual ~VulkanBase();

	void WaitIdle() const;

	VulkanBase(const VulkanBase&) = delete;
	VulkanBase& operator=(const VulkanBase&) = delete;

protected:

	/** @name Core handles */
	///@{
	GLFWwindow* window = nullptr;
	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue presentQueue = VK_NULL_HANDLE;
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
	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;
	uint32_t currentFrame = 0;
	bool framebufferResized = false;
	///@}

	/** @name Sprite SSBO */
	///@{
	VkDescriptorSetLayout spriteSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool spriteDescriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> spriteDescriptorSets;
	std::vector<VkBuffer> spriteBuffers;
	std::vector<VkDeviceMemory> spriteBufferMemory;
	std::vector<void*> spriteBufferMapped;
	///@}

	/** @name Feature flags */
	///@{
	bool validationEnabled = false;
	bool wireframeEnabled = false;
	///@}

	/**
	 * Updates the wireframe flag. RecreateSwapchain must be called
	 * afterwards for the pipeline to reflect the change.
	 */
	void SetWireframe(bool enabled)
	{
		wireframeEnabled = enabled;
	}

	/**
	 * Groups the objects a subclass needs to record a frame.
	 * Filled by BeginFrame, consumed by the render-pass helpers and EndFrame.
	 */
	struct FrameContext
	{
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		uint32_t        imageIndex = 0;
	};

	/** @name Per-frame helpers */
	///@{

	/**
	 * Waits on the in-flight fence, acquires the next swapchain image,
	 * resets the command buffer, and calls vkBeginCommandBuffer.
	 * Returns false if the frame should be skipped (minimised or out-of-date).
	 */
	bool BeginFrame(FrameContext& out);

	/**
	 * Uploads scene data to the SSBO and begins the render pass.
	 */
	void BeginRenderPass(const FrameContext& ctx, const SpriteList& scene);

	/**
	 * Binds the pipeline and sprite descriptor set, then issues vkCmdDraw
	 * for all sprites. Call between BeginRenderPass and EndRenderPass.
	 */
	void DrawSprites(const FrameContext& ctx, const SpriteList& scene);

	/**
	 * Ends the render pass.
	 */
	void EndRenderPass(const FrameContext& ctx);

	/**
	 * Calls vkEndCommandBuffer, submits to the graphics queue, presents,
	 * and advances currentFrame.
	 */
	void EndFrame(const FrameContext& ctx);

	///@}

	/** @name Setup */
	///@{
	void CreateInstance();
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

	/**
	 * Tears down swapchain resources in reverse creation order.
	 */
	void CleanupSwapchain();

	/**
	 * Tears down and rebuilds everything swapchain-dependent.
	 * Returns false if the window is minimised and recreation was skipped.
	 */
	bool RecreateSwapchain();
	///@}

	/** @name Sprite SSBO setup */
	///@{
	void CreateSpriteSetLayout();
	void CreateSpriteBuffers();
	void CreateSpriteDescriptorPool();
	void CreateSpriteDescriptorSets();
	///@}

	/** @name Helpers */
	///@{

	/**
	 * Returns the index of the first memory type satisfying both
	 * typeFilter and the requested property flags.
	 */
	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

	/**
	 * Loads a compiled SPIR-V shader file from disk.
	 */
	static std::vector<char> ReadFile(const std::string& path);

	[[nodiscard]]
	VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

	/**
	 * Throws std::runtime_error if result is not VK_SUCCESS,
	 * including the result code name in the message.
	 */
	static void VkCheck(VkResult result, const char* msg);

	/**
	 * GLFW framebuffer resize callback. Sets framebufferResized = true.
	 */
	static void FramebufferResizeCallback(GLFWwindow* window, int, int);

	///@}
};