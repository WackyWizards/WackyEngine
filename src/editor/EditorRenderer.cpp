#include "editor/EditorRenderer.h"
#include <iostream>
#include <fstream>

extern std::ofstream g_log;
static void RLog(const char* msg)
{
	if (g_log.is_open())
	{
		g_log << msg << "\n";
		g_log.flush();
	}
}

/**
 * Debug messenger (editor-only Vulkan callback)
 */

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cout << "[Vulkan] " << data->pMessage << "\n\n";
	}

	return VK_FALSE;
}

static VkDebugUtilsMessengerEXT s_debugMessenger = VK_NULL_HANDLE;

static void CreateDebugMessenger(const VkInstance instance)
{
	VkDebugUtilsMessengerCreateInfoEXT ci{};
	ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	ci.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	ci.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	ci.pfnUserCallback = DebugCallback;

	auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

	if (!fn || fn(instance, &ci, nullptr, &s_debugMessenger) != VK_SUCCESS)
	{
		std::cout << "[Warning] Could not create debug messenger.\n";
	}
}

static void DestroyDebugMessenger(const VkInstance instance)
{
	if (s_debugMessenger == VK_NULL_HANDLE)
	{
		return;
	}

	auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

	if (fn)
	{
		fn(instance, s_debugMessenger, nullptr);
	}
}

Renderer::Renderer(
	GLFWwindow* w,
	std::function<void()> onReload,
	std::function<void()> onBuildAndReload,
	std::function<void()> onExport,
	std::function<void(const std::string&, const std::string&)> onNewProject,
	std::function<void(const std::string&)> onLoadProject,
	std::function<std::vector<EntityTypeInfo>()> onGetEntityTypes)
	: VulkanBase(w,
#ifdef NDEBUG
		/*validation=*/ false,
#else
		/*validation=*/ true,
#endif
		/*wireframe=*/ false)
	, onReload{ std::move(onReload) }
	, onBuildAndReload{ std::move(onBuildAndReload) }
	, onExport{ std::move(onExport) }
	, onNewProject{ std::move(onNewProject) }
	, onLoadProject{ std::move(onLoadProject) }
	, onGetEntityTypes{ std::move(onGetEntityTypes) }
{
	if (validationEnabled)
	{
		RLog("CreateDebugMessenger");
		CreateDebugMessenger(instance);
	}

	RLog("CreateEngineUI");
	engineUI = std::make_unique<EngineUI>(
		window,
		instance,
		physicalDevice,
		device,
		graphicsFamily,
		graphicsQueue,
		renderPass,
		MAX_FRAMES_IN_FLIGHT,
		this->onReload,
		this->onBuildAndReload,
		this->onExport,
		this->onNewProject,
		this->onLoadProject,
		this->onGetEntityTypes
	);
}

Renderer::~Renderer()
{
	/**
	 * WaitIdle is called by the VulkanBase destructor, but the UI must be
	 * torn down before any Vulkan resources are released.
	 */
	vkDeviceWaitIdle(device);
	engineUI.reset();

	if (validationEnabled)
	{
		DestroyDebugMessenger(instance);
	}

	/** ~VulkanBase() handles the rest. */
}

void Renderer::DrawFrame(const SpriteList& scene, const EditorStats& stats, World& world)
{
	FrameContext ctx;

	if (!BeginFrame(ctx))
	{
		return;
	}

	// Compute camera transform
	const float windowW = static_cast<float>(swapchainExtent.width);
	const float windowH = static_cast<float>(swapchainExtent.height);
	const float menuBarH = engineUI->GetMenuBarHeight();
	const float leftW = engineUI->IsHierarchyVisible() ? 260.f : 0.f;
	const float rightW = engineUI->IsPropertiesVisible() ? 300.f : 0.f;
	const float vpW = std::max(1.f, windowW - leftW - rightW);
	const float vpH = std::max(1.f, windowH - menuBarH);
	const float cx = leftW + vpW * 0.5f; // screen-space viewport center
	const float cy = menuBarH + vpH * 0.5f;
	const float zoom = engineUI->GetVpZoom();

	CameraUniforms cam{};
	cam.ndcCenterX = cx / (windowW * 0.5f) - 1.f;
	cam.ndcCenterY = cy / (windowH * 0.5f) - 1.f;
	cam.scaleX = zoom / (windowW * 0.5f);
	cam.scaleY = zoom / (windowH * 0.5f);
	cam.panX = engineUI->GetVpPanX();
	cam.panY = engineUI->GetVpPanY();

	BeginRenderPass(ctx, scene);
	DrawSprites(ctx, scene, cam);
	engineUI->Draw(ctx.cmd, stats, world);
	EndRenderPass(ctx);
	EndFrame(ctx);

	if (engineUI->IsWireframe() != wireframeEnabled)
	{
		SetWireframe(engineUI->IsWireframe());
		RecreateSwapchain();
	}
}

/**
 * EngineUI passthroughs
 */

bool Renderer::HasPendingWorldLoad() const
{
	return engineUI->HasPendingWorldLoad();
}

std::string Renderer::GetPendingWorldLoadPath() const
{
	return engineUI->PendingWorldLoadPath();
}

void Renderer::ClearPendingWorldLoad() const
{
	engineUI->ClearPendingWorldLoad();
}

PlayState Renderer::GetPlayState() const
{
	return engineUI->GetPlayState();
}

float Renderer::GetTimescale() const
{
	return engineUI->GetTimescale();
}

float Renderer::GetFixedStep() const
{
	return engineUI->GetFixedStep();
}

bool Renderer::ConsumeStepFrame() const
{
	return engineUI->ConsumeStepFrame();
}

void Renderer::NotifyWorldRestored() const
{
	engineUI->NotifyWorldRestored();
}
