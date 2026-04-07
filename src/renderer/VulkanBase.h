#pragma once

#include "core/Game.h"

struct CameraUniforms {
	/** Viewport center in NDC */
	float ndcCenterX, ndcCenterY;
	/** Zoom / half-window-size */
	float scaleX, scaleY;
	/** World position at viewport center */
	float panX, panY;
};

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

	/**
	 * Loads a PNG from disk, uploads it to a DEVICE_LOCAL VkImage,
	 * and returns the fully initialized Texture.
	 * Uses NEAREST filtering, suitable for pixel-art sprites for now.
	 * The caller is responsible for calling DestroyTexture when done.
	 */
	Texture LoadTexture(const std::string& path) const;

	/**
	 * Releases all Vulkan objects owned by tex and resets it to the default state.
	 */
	void DestroyTexture(Texture& tex) const;

	/**
	 * Updates the combined-image-sampler descriptor (binding 1) on all
	 * per-frame descriptor sets to point at tex.
	 * Call once after loading a texture, before the next DrawSprites.
	 */
	void BindTexture(const Texture& tex) const;

	/**
	 * Uploads a 1x1 opaque-white pixel as the fallback texture.
	 * Used when texture loading fails, so the game continues with a white placeholder.
	 */
	[[nodiscard]]
	Texture CreateWhiteTexture() const;

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
	VkCommandPool commandPool = VK_NULL_HANDLE;
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

	/** @name Textures */
	///@{
	/**
	 * 1x1 white fallback bound at startup.
	 * Ensures binding 1 is always valid even when no game texture is loaded.
	 */
	Texture m_whiteTexture;
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
	void SetWireframe(const bool enabled)
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
		uint32_t imageIndex = 0;
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
	void BeginRenderPass(const FrameContext& ctx, const SpriteList& scene) const;

	/**
	 * Binds the pipeline and sprite descriptor set, then issues vkCmdDraw
	 * for all sprites. Call between BeginRenderPass and EndRenderPass.
	 */
	void DrawSprites(const FrameContext& ctx, const SpriteList& scene, const CameraUniforms& cam) const;

	/**
	 * Ends the render pass.
	 */
	static void EndRenderPass(const FrameContext& ctx);

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

	/** @name Texture helpers */
	///@{

	/**
	 * Allocates and begins a one-shot command buffer on the graphics queue.
	 * Must be closed with EndSingleTimeCommands.
	 */
	VkCommandBuffer BeginSingleTimeCommands() const;

	/**
	 * Ends, submits, and frees the command buffer returned by BeginSingleTimeCommands.
	 * Blocks until the queue is idle.
	 */
	void EndSingleTimeCommands(VkCommandBuffer cmd) const;

	/**
	 * Creates a DEVICE_LOCAL VkImage and allocates its backing memory.
	 */
	void CreateImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkImage& outImage, VkDeviceMemory& outMemory) const;

	/**
	 * Records a pipeline barrier that transitions image between the two layouts.
	 * Only the UNDEFINED->TRANSFER_DST and TRANSFER_DST->SHADER_READ_ONLY transitions are supported.
	 */
	void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const;

	/**
	 * Copies the entire buffer into the image, which must already be in TRANSFER_DST_OPTIMAL layout.
	 */
	void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) const;

	/**
	 * Creates a 2D VkImageView for a texture in R8G8B8A8_SRGB format.
	 */
	[[nodiscard]]
	VkImageView CreateTextureImageView(VkImage image) const;

	/**
	 * Creates a VkSampler using NEAREST filtering and CLAMP_TO_EDGE.
	 * NEAREST preserves pixel-art sharpness.
	 */
	[[nodiscard]]
	VkSampler CreateTextureSampler() const;

	///@}
};