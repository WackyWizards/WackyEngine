#include "renderer\Renderer.h"
#include <iostream>
#include <fstream>
#include <cstring>

extern std::ofstream g_log;
static void RLog(const char* msg)
{
	if (g_log.is_open())
	{
		g_log << msg << "\n";
		g_log.flush();
	}
}

#ifdef NDEBUG
static constexpr bool VALIDATION = false;
#else
static constexpr bool VALIDATION = true;
#endif

namespace
{
	auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
}

void Renderer::VkCheck(const VkResult result, const char* msg)
{
	if (result == VK_SUCCESS)
	{
		return;
	}

	// Convert the result code to a readable string
	auto resultStr = "UNKNOWN";
	switch (result)
	{
	case VK_ERROR_OUT_OF_HOST_MEMORY:
		resultStr = "VK_ERROR_OUT_OF_HOST_MEMORY";
		break;
	case VK_ERROR_OUT_OF_DEVICE_MEMORY:
		resultStr = "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		break;
	case VK_ERROR_INITIALIZATION_FAILED:
		resultStr = "VK_ERROR_INITIALIZATION_FAILED";
		break;
	case VK_ERROR_DEVICE_LOST:
		resultStr = "VK_ERROR_DEVICE_LOST";
		break;
	case VK_ERROR_INCOMPATIBLE_DRIVER:
		resultStr = "VK_ERROR_INCOMPATIBLE_DRIVER";
		break;
	case VK_ERROR_EXTENSION_NOT_PRESENT:
		resultStr = "VK_ERROR_EXTENSION_NOT_PRESENT";
		break;
	case VK_ERROR_FEATURE_NOT_PRESENT:
		resultStr = "VK_ERROR_FEATURE_NOT_PRESENT";
		break;
	case VK_ERROR_FORMAT_NOT_SUPPORTED:
		resultStr = "VK_ERROR_FORMAT_NOT_SUPPORTED";
		break;
	case VK_ERROR_SURFACE_LOST_KHR:
		resultStr = "VK_ERROR_SURFACE_LOST_KHR";
		break;
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
		resultStr = "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		break;
	case VK_ERROR_OUT_OF_DATE_KHR:
		resultStr = "VK_ERROR_OUT_OF_DATE_KHR";
		break;
	case VK_ERROR_VALIDATION_FAILED_EXT:
		resultStr = "VK_ERROR_VALIDATION_FAILED_EXT";
		break;
	case VK_ERROR_INVALID_SHADER_NV:
		resultStr = "VK_ERROR_INVALID_SHADER_NV";
		break;
	default:
		break;
	}

	throw std::runtime_error(std::string(msg) + " [" + resultStr + "]");
}

VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
	// Only print warnings and errors, ignore verbose info spam
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cout << "[Vulkan] " << data->pMessage << "\n\n";
	}
	return VK_FALSE;
}

std::vector<char> Renderer::ReadFile(const std::string& path)
{
	std::ifstream f(path, std::ios::ate | std::ios::binary);

	if (!f.is_open())
	{
		throw std::runtime_error("Cannot open shader: " + path);
	}

	const size_t size = f.tellg();
	std::vector<char> buf(size);
	f.seekg(0);
	f.read(buf.data(), size);
	return buf;
}

VkShaderModule Renderer::CreateShaderModule(const std::vector<char>& code) const
{
	VkShaderModuleCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	ci.codeSize = code.size();
	ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
	VkShaderModule mod;
	VkCheck(vkCreateShaderModule(device, &ci, nullptr, &mod), "vkCreateShaderModule");
	return mod;
}

Renderer::Renderer(GLFWwindow* w,
	std::function<void()> onReload,
	std::function<void()> onBuildAndReload,
	std::function<void(const std::string&, const std::string&)> onNewProject,
	std::function<void(const std::string&)> onLoadProject)
	: window(w)
	, onReload(std::move(onReload))
	, onBuildAndReload(std::move(onBuildAndReload))
	, onNewProject(std::move(onNewProject))
	, onLoadProject(std::move(onLoadProject))
{
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);

	RLog("CreateInstance");
	CreateInstance();
	RLog("CreateDebugMessenger");
	CreateDebugMessenger();
	RLog("CreateSurface");
	CreateSurface();
	RLog("PickPhysicalDevice");
	PickPhysicalDevice();
	RLog("CreateLogicalDevice");
	CreateLogicalDevice();

	RLog("CreateSpriteSetLayout");
	CreateSpriteSetLayout();
	RLog("CreateSpriteBuffers");
	CreateSpriteBuffers();
	RLog("CreateSpriteDescriptorPool");
	CreateSpriteDescriptorPool();
	RLog("CreateSpriteDescriptorSets");
	CreateSpriteDescriptorSets();

	RLog("CreateSwapchain");
	CreateSwapchain();
	RLog("CreateImageViews");
	CreateImageViews();
	RLog("CreateRenderPass");
	CreateRenderPass();
	RLog("CreateGraphicsPipeline");
	CreateGraphicsPipeline();
	RLog("CreateFramebuffers");
	CreateFramebuffers();
	RLog("CreateCommandPool");
	CreateCommandPool();
	RLog("CreateCommandBuffers");
	CreateCommandBuffers();
	RLog("CreateSyncObjects");
	CreateSyncObjects();

	RLog("CreateEngineUI");
	engineUI = std::make_unique<EngineUI>(
		window,
		instance,
		physicalDevice,
		device,
		graphicsFamily,
		graphicsQueue, renderPass,
		MAX_FRAMES_IN_FLIGHT,
		this->onReload,
		this->onBuildAndReload,
		this->onNewProject,
		this->onLoadProject
	);
}

Renderer::~Renderer()
{
	vkDeviceWaitIdle(device);
	engineUI.reset();
	CleanupSwapchain();

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
		vkDestroyFence(device, inFlightFences[i], nullptr);
	}

	for (size_t i = 0; i < renderFinishedSemaphores.size(); i++)
	{
		vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
	}

	vkDestroyCommandPool(device, commandPool, nullptr);

	// Sprite SSBO cleanup. Unmap before destroying the memory.
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkUnmapMemory(device, spriteBufferMemory[i]);
		vkDestroyBuffer(device, spriteBuffers[i], nullptr);
		vkFreeMemory(device, spriteBufferMemory[i], nullptr);
	}
	// Freeing the pool implicitly frees all descriptor sets allocated from it.
	vkDestroyDescriptorPool(device, spriteDescriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, spriteSetLayout, nullptr);

	vkDestroyDevice(device, nullptr);

	if (VALIDATION)
	{
		auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

		if (fn)
		{
			fn(instance, debugMessenger, nullptr);
		}
	}

	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
}

void Renderer::FramebufferResizeCallback(GLFWwindow* window, int, int)
{
	const auto renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
	renderer->framebufferResized = true;
}

void Renderer::CreateInstance()
{
	// Check validation layer is available before requesting it
	if (VALIDATION)
	{
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> layers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

		bool found = false;
		for (const auto& layer : layers)
		{
			if (strcmp(layer.layerName, VALIDATION_LAYER) == 0)
			{
				found = true; break;
			}
		}

		if (!found)
		{
			std::cout << "[Warning] Validation layer requested but not available. Are you missing the VulkanSDK?";
		}
	}

	VkApplicationInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	ai.pApplicationName = "WackyEngine";
	ai.apiVersion = VK_API_VERSION_1_3;

	uint32_t glfwExtCount = 0;
	const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
	std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);

	if (VALIDATION)
	{
		exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.pApplicationInfo = &ai;
	ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
	ci.ppEnabledExtensionNames = exts.data();

	if (VALIDATION)
	{
		ci.enabledLayerCount = 1;
		ci.ppEnabledLayerNames = &VALIDATION_LAYER;
	}

	VkCheck(vkCreateInstance(&ci, nullptr, &instance), "vkCreateInstance");
}

void Renderer::CreateDebugMessenger()
{
	if (!VALIDATION)
	{
		return;
	}

	VkDebugUtilsMessengerCreateInfoEXT ci{};
	ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	ci.pfnUserCallback = DebugCallback;

	// This function isn't statically linked, so we should load it at runtime
	auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

	if (!fn || fn(instance, &ci, nullptr, &debugMessenger) != VK_SUCCESS)
	{
		std::cout << "[Warning] Could not create debug messenger.\n";
	}
}

void Renderer::CreateSurface()
{
	VkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
}

void Renderer::PickPhysicalDevice()
{
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, nullptr);
	std::vector<VkPhysicalDevice> devices(count);
	vkEnumeratePhysicalDevices(instance, &count, devices.data());

	for (const auto pd : devices)
	{
		uint32_t qc = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr);
		std::vector<VkQueueFamilyProperties> qf(qc);
		vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qf.data());

		bool foundG = false;
		bool foundP = false;
		for (uint32_t i = 0; i < qc; i++)
		{
			if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				graphicsFamily = i;
				foundG = true;
			}

			VkBool32 present = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);

			if (present)
			{
				presentFamily = i; foundP = true;
			}
		}

		if (foundG && foundP)
		{
			physicalDevice = pd; return;
		}
	}

	throw std::runtime_error("No suitable GPU found");
}

void Renderer::CreateLogicalDevice()
{
	const float priority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> qcis;
	for (const uint32_t fam : {graphicsFamily, presentFamily})
	{
		bool already = false;
		for (const auto& q : qcis) if (q.queueFamilyIndex == fam)
		{
			already = true;
			break;
		}

		if (already)
		{
			continue;
		}

		VkDeviceQueueCreateInfo qi{};
		qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		qi.queueFamilyIndex = fam;
		qi.queueCount = 1;
		qi.pQueuePriorities = &priority;
		qcis.push_back(qi);
	}

	// Required for SV_VertexID in Slang/HLSL shaders
	VkPhysicalDeviceVulkan11Features features11{};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.shaderDrawParameters = VK_TRUE;
	
	// Required for VK_POLYGON_MODE_LINE (wireframe)
	VkPhysicalDeviceFeatures features{};
	features.fillModeNonSolid = VK_TRUE;

	const auto devExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	VkDeviceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	ci.pNext = &features11;
	ci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
	ci.pQueueCreateInfos = qcis.data();
	ci.enabledExtensionCount = 1;
	ci.ppEnabledExtensionNames = &devExt;
	ci.pEnabledFeatures = &features;

	if (VALIDATION)
	{
		ci.enabledLayerCount = 1;
		ci.ppEnabledLayerNames = &VALIDATION_LAYER;
	}

	VkCheck(vkCreateDevice(physicalDevice, &ci, nullptr, &device), "vkCreateDevice");
	vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void Renderer::CreateCommandPool()
{
	VkCommandPoolCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	ci.queueFamilyIndex = graphicsFamily;
	VkCheck(vkCreateCommandPool(device, &ci, nullptr, &commandPool), "vkCreateCommandPool");
}

void Renderer::CreateSyncObjects()
{
	// acquire semaphores and fences: one per in-flight frame, indexed by currentFrame.
	// renderFinished semaphores: one per SWAPCHAIN IMAGE, indexed by imageIndex.
	// vkQueuePresentKHR waits on these, and the spec only guarantees they are safe
	// to reuse once vkAcquireNextImageKHR returns that same image index again.
	// Indexing by frame-in-flight instead violates this guarantee.
	imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	renderFinishedSemaphores.resize(swapchainImages.size());
	inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

	constexpr VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkCheck(vkCreateSemaphore(device, &si, nullptr, &imageAvailableSemaphores[i]), "vkCreateSemaphore imageAvailable");
		VkCheck(vkCreateFence(device, &fi, nullptr, &inFlightFences[i]), "vkCreateFence");
	}

	for (size_t i = 0; i < swapchainImages.size(); i++)
	{
		VkCheck(vkCreateSemaphore(device, &si, nullptr, &renderFinishedSemaphores[i]), "vkCreateSemaphore renderFinished");
	}
}

uint32_t Renderer::FindMemoryType(const uint32_t typeFilter, const VkMemoryPropertyFlags props) const
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
	{
		if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
		{
			return i;
		}
	}

	throw std::runtime_error("FindMemoryType: no suitable memory type found");
}

void Renderer::CreateSpriteSetLayout()
{
	// Binding 0: the SpriteList storage buffer, visible to the vertex shader.
	// In HLSL/Slang:  [[vk::binding(0,0)]] StructuredBuffer<SpriteList> spriteData;
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	ci.bindingCount = 1;
	ci.pBindings = &binding;
	VkCheck(vkCreateDescriptorSetLayout(device, &ci, nullptr, &spriteSetLayout), "vkCreateDescriptorSetLayout sprite");
}

void Renderer::CreateSpriteBuffers()
{
	// One host-visible, persistently-mapped buffer per frame in flight.
	// HOST_COHERENT means writes are visible to the GPU without an explicit flush.

	spriteBuffers.resize(MAX_FRAMES_IN_FLIGHT);
	spriteBufferMemory.resize(MAX_FRAMES_IN_FLIGHT);
	spriteBufferMapped.resize(MAX_FRAMES_IN_FLIGHT);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		constexpr VkDeviceSize bufSize = sizeof(SpriteList);
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = bufSize;
		bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkCheck(vkCreateBuffer(device, &bci, nullptr, &spriteBuffers[i]), "vkCreateBuffer sprite");

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(device, spriteBuffers[i], &memReq);

		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = memReq.size;
		mai.memoryTypeIndex = FindMemoryType(
			memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		VkCheck(vkAllocateMemory(device, &mai, nullptr, &spriteBufferMemory[i]), "vkAllocateMemory sprite");

		vkBindBufferMemory(device, spriteBuffers[i], spriteBufferMemory[i], 0);
		vkMapMemory(device, spriteBufferMemory[i], 0, bufSize, 0, &spriteBufferMapped[i]);
	}
}

void Renderer::CreateSpriteDescriptorPool()
{
	VkDescriptorPoolSize poolSize;
	poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

	VkDescriptorPoolCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	ci.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ci.poolSizeCount = 1;
	ci.pPoolSizes = &poolSize;
	VkCheck(vkCreateDescriptorPool(device, &ci, nullptr, &spriteDescriptorPool), "vkCreateDescriptorPool sprite");
}

void Renderer::CreateSpriteDescriptorSets()
{
	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, spriteSetLayout);

	VkDescriptorSetAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ai.descriptorPool = spriteDescriptorPool;
	ai.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ai.pSetLayouts = layouts.data();

	spriteDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
	VkCheck(vkAllocateDescriptorSets(device, &ai, spriteDescriptorSets.data()), "vkAllocateDescriptorSets sprite");

	// Point each descriptor set at its corresponding buffer.
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorBufferInfo bufInfo{};
		bufInfo.buffer = spriteBuffers[i];
		bufInfo.offset = 0;
		bufInfo.range = sizeof(SpriteList);

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = spriteDescriptorSets[i];
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &bufInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}
}

void Renderer::CreateSwapchain()
{
	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

	swapchainExtent = caps.currentExtent;
	swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;

	VkSwapchainCreateInfoKHR ci{};
	ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	ci.surface = surface;
	ci.minImageCount = caps.minImageCount + 1;
	ci.imageFormat = swapchainFormat;
	ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	ci.imageExtent = swapchainExtent;
	ci.imageArrayLayers = 1;
	ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ci.preTransform = caps.currentTransform;
	ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	ci.clipped = VK_TRUE;
	ci.oldSwapchain = VK_NULL_HANDLE;

	VkCheck(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain), "vkCreateSwapchainKHR");

	uint32_t imgCount = 0;
	vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
	swapchainImages.resize(imgCount);
	vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapchainImages.data());
}

void Renderer::CreateImageViews()
{
	swapchainImageViews.resize(swapchainImages.size());
	for (size_t i = 0; i < swapchainImages.size(); i++)
	{
		VkImageViewCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ci.image = swapchainImages[i];
		ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ci.format = swapchainFormat;
		ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ci.subresourceRange.levelCount = 1;
		ci.subresourceRange.layerCount = 1;

		VkCheck(vkCreateImageView(device, &ci, nullptr, &swapchainImageViews[i]), "vkCreateImageView");
	}
}

void Renderer::CreateRenderPass()
{
	VkAttachmentDescription att{};
	att.format = swapchainFormat;
	att.samples = VK_SAMPLE_COUNT_1_BIT;
	att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference ref{};
	ref.attachment = 0;
	ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription sub{};
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.colorAttachmentCount = 1;
	sub.pColorAttachments = &ref;

	VkSubpassDependency dep{};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	ci.attachmentCount = 1;
	ci.pAttachments = &att;
	ci.subpassCount = 1;
	ci.pSubpasses = &sub;
	ci.dependencyCount = 1;
	ci.pDependencies = &dep;

	VkCheck(vkCreateRenderPass(device, &ci, nullptr, &renderPass), "vkCreateRenderPass");
}

void Renderer::CreateGraphicsPipeline()
{
	std::vector<char> vert = ReadFile("vert.spv");
	std::vector<char> frag = ReadFile("frag.spv");
	VkShaderModule vMod = CreateShaderModule(vert);
	VkShaderModule fMod = CreateShaderModule(frag);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vMod;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fMod;
	stages[1].pName = "main";

	// No push constants: sprite data now comes from the SSBO at set 0, binding 0.
	// The shader must declare:
	//   [[vk::binding(0,0)]] StructuredBuffer<SpriteList> spriteData;
	// and index it as:  SpriteList list = spriteData[0];
	VkPipelineLayoutCreateInfo layoutCI{};
	layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &spriteSetLayout;
	VkCheck(vkCreatePipelineLayout(device, &layoutCI, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{ 0, 0, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0, 1 };
	VkRect2D   scissor{ {0, 0}, swapchainExtent };
	VkPipelineViewportStateCreateInfo vps{};
	vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vps.viewportCount = 1; vps.pViewports = &viewport;
	vps.scissorCount = 1; vps.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rast{};
	rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rast.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	rast.cullMode = VK_CULL_MODE_NONE;
	rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rast.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments = &cba;

	VkGraphicsPipelineCreateInfo pci{};
	pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pci.stageCount = 2;
	pci.pStages = stages;
	pci.pVertexInputState = &vi;
	pci.pInputAssemblyState = &ia;
	pci.pViewportState = &vps;
	pci.pRasterizationState = &rast;
	pci.pMultisampleState = &ms;
	pci.pColorBlendState = &cb;
	pci.layout = pipelineLayout;
	pci.renderPass = renderPass;

	VkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &graphicsPipeline), "vkCreateGraphicsPipelines");
	vkDestroyShaderModule(device, vMod, nullptr);
	vkDestroyShaderModule(device, fMod, nullptr);
}

void Renderer::CreateFramebuffers()
{
	framebuffers.resize(swapchainImageViews.size());
	for (size_t i = 0; i < swapchainImageViews.size(); i++)
	{
		VkFramebufferCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		ci.renderPass = renderPass;
		ci.attachmentCount = 1;
		ci.pAttachments = &swapchainImageViews[i];
		ci.width = swapchainExtent.width;
		ci.height = swapchainExtent.height;
		ci.layers = 1;
		VkCheck(vkCreateFramebuffer(device, &ci, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
	}
}

void Renderer::CreateCommandBuffers()
{
	commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
	VkCommandBufferAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ai.commandPool = commandPool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
	VkCheck(vkAllocateCommandBuffers(device, &ai, commandBuffers.data()), "vkAllocateCommandBuffers");
}

void Renderer::CleanupSwapchain()
{
	for (const VkFramebuffer frameBuffer : framebuffers)
	{
		vkDestroyFramebuffer(device, frameBuffer, nullptr);
	}

	for (const VkImageView imageView : swapchainImageViews)
	{
		vkDestroyImageView(device, imageView, nullptr);
	}

	vkDestroyPipeline(device, graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);

	framebuffers.clear();
	swapchainImageViews.clear();
	swapchainImages.clear();
}

bool Renderer::RecreateSwapchain()
{
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(window, &width, &height);

	// Window is minimized — skip recreation this frame.
	// DrawFrame will retry next tick naturally without blocking.
	if (width == 0 || height == 0)
	{
		return false;
	}

	vkDeviceWaitIdle(device);
	CleanupSwapchain();
	CreateSwapchain();
	CreateImageViews();

	// renderFinishedSemaphores are sized per swapchain image. If the image count changed
	// after recreation, destroy the old ones and create new ones.
	for (const VkSemaphore semaphore : renderFinishedSemaphores)
	{
		vkDestroySemaphore(device, semaphore, nullptr);
	}

	renderFinishedSemaphores.clear();
	constexpr VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	renderFinishedSemaphores.resize(swapchainImages.size());
	for (size_t i = 0; i < swapchainImages.size(); i++)
	{
		VkCheck(vkCreateSemaphore(device, &si, nullptr, &renderFinishedSemaphores[i]), "vkCreateSemaphore renderFinished");
	}

	CreateRenderPass();

	// spriteSetLayout is device-lifetime and survives swapchain recreation;
	// CreateGraphicsPipeline reuses it directly.
	CreateGraphicsPipeline();
	CreateFramebuffers();
	CreateCommandBuffers();
	return true;
}

void Renderer::DrawFrame(const SpriteList& scene, const EditorStats& stats)
{
	const VkFence fence = inFlightFences[currentFrame];
	vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

	uint32_t imageIndex = 0;
	const VkResult nextImageKHR = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

	if (nextImageKHR == VK_ERROR_OUT_OF_DATE_KHR || nextImageKHR == VK_SUBOPTIMAL_KHR)
	{
		RLog("DF:RecreateSwapchain (acquire)");
		RecreateSwapchain(); return;
	}

	vkResetFences(device, 1, &fence);

	// Upload this frame's scene data into the matching SSBO slot.
	// The fence above guarantees the GPU is no longer reading from this slot.
	// HOST_COHERENT memory requires no explicit flush.
	memcpy(spriteBufferMapped[currentFrame], &scene, sizeof(SpriteList));

	const VkCommandBuffer cmd = commandBuffers[currentFrame];
	vkResetCommandBuffer(cmd, 0);

	const VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	vkBeginCommandBuffer(cmd, &bi);

	constexpr VkClearValue clear{ .color = {0.05f, 0.05f, 0.08f, 1.0f} };
	VkRenderPassBeginInfo rpbi{};
	rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpbi.renderPass = renderPass;
	rpbi.framebuffer = framebuffers[imageIndex];
	rpbi.renderArea.extent = swapchainExtent;
	rpbi.clearValueCount = 1;
	rpbi.pClearValues = &clear;

	vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

	// Bind the sprite SSBO descriptor set for this frame slot.
	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipelineLayout,
		0,
		1, &spriteDescriptorSets[currentFrame],
		0, nullptr
	);

	if (scene.count > 0)
	{
		vkCmdDraw(cmd, scene.count * 6, 1, 0, 0); // 6 verts per quad (2 triangles)
	}

	engineUI->Draw(cmd, stats);

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);

	// acquire semaphore: per frame-in-flight, safe because the fence above ensures
	// this frame's previous acquire has already been waited on by submit.
	const VkSemaphore waitSemaphore = imageAvailableSemaphores[currentFrame];

	// renderFinished semaphore: MUST be per swapchain image, indexed by imageIndex.
	// vkQueuePresentKHR waits on this semaphore but provides no way to signal when it's done.
	// The only guarantee of reuse safety is that vkAcquireNextImageKHR
	// returning this imageIndex means the previous present for that image is complete.
	const VkSemaphore signalSemaphore = renderFinishedSemaphores[imageIndex];

	constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &waitSemaphore;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &signalSemaphore;
	VkCheck(vkQueueSubmit(graphicsQueue, 1, &submit, fence), "vkQueueSubmit");

	VkPresentInfoKHR present{};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &signalSemaphore;
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &imageIndex;
	const VkResult queuePresentKHRResult = vkQueuePresentKHR(presentQueue, &present);

	if (queuePresentKHRResult == VK_ERROR_OUT_OF_DATE_KHR || queuePresentKHRResult == VK_SUBOPTIMAL_KHR || framebufferResized)
	{
		framebufferResized = false;

		// minimized, skip
		if (!RecreateSwapchain())
		{
			return;
		}
	}
	else if (engineUI->IsWireframe() != wireframe)
	{
		// Wireframe was toggled - rebuild the pipeline with the new polygon mode.
		wireframe = engineUI->IsWireframe();
		RecreateSwapchain();
	}

	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::WaitIdle() const
{
	vkDeviceWaitIdle(device);
}