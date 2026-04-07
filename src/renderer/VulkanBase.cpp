#include "renderer/VulkanBase.h"
#include <iostream>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <filesystem>

namespace
{
	auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
}

/**
 * Helpers
 */

void VulkanBase::VkCheck(const VkResult result, const char* msg)
{
	if (result == VK_SUCCESS)
	{
		return;
	}

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
	case VK_ERROR_MEMORY_MAP_FAILED:
		resultStr = "VK_ERROR_MEMORY_MAP_FAILED";
		break;
	case VK_ERROR_LAYER_NOT_PRESENT:
		resultStr = "VK_ERROR_LAYER_NOT_PRESENT";
		break;
	case VK_ERROR_EXTENSION_NOT_PRESENT:
		resultStr = "VK_ERROR_EXTENSION_NOT_PRESENT";
		break;
	case VK_ERROR_FEATURE_NOT_PRESENT:
		resultStr = "VK_ERROR_FEATURE_NOT_PRESENT";
		break;
	case VK_ERROR_INCOMPATIBLE_DRIVER:
		resultStr = "VK_ERROR_INCOMPATIBLE_DRIVER";
		break;
	case VK_ERROR_TOO_MANY_OBJECTS:
		resultStr = "VK_ERROR_TOO_MANY_OBJECTS";
		break;
	case VK_ERROR_FORMAT_NOT_SUPPORTED:
		resultStr = "VK_ERROR_FORMAT_NOT_SUPPORTED";
		break;
	case VK_ERROR_FRAGMENTED_POOL:
		resultStr = "VK_ERROR_FRAGMENTED_POOL";
		break;
	case VK_ERROR_OUT_OF_POOL_MEMORY:
		resultStr = "VK_ERROR_OUT_OF_POOL_MEMORY";
		break;
	case VK_ERROR_INVALID_EXTERNAL_HANDLE:
		resultStr = "VK_ERROR_INVALID_EXTERNAL_HANDLE";
		break;
	case VK_ERROR_FRAGMENTATION:
		resultStr = "VK_ERROR_FRAGMENTATION";
		break;
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
		resultStr = "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
		break;
	case VK_ERROR_UNKNOWN:
		resultStr = "VK_ERROR_UNKNOWN";
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

std::vector<char> VulkanBase::ReadFile(const std::string& path)
{
	std::cout << "[VulkanBase] Loading shader: " << std::filesystem::absolute(path) << "\n";
	std::ifstream f(path, std::ios::ate | std::ios::binary);

	if (!f.is_open())
	{
		throw std::runtime_error("Cannot open shader: " + path);
	}

	const std::streamsize size = f.tellg();

	if (size < 0)
	{
		throw std::runtime_error("Failed to get file size");
	}

	std::vector<char> buf(static_cast<size_t>(size));

	f.seekg(0);
	f.read(buf.data(), size);

	return buf;
}

VkShaderModule VulkanBase::CreateShaderModule(const std::vector<char>& code) const
{
	VkShaderModuleCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	ci.codeSize = code.size();
	ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule mod;
	VkCheck(vkCreateShaderModule(device, &ci, nullptr, &mod), "vkCreateShaderModule");
	return mod;
}

void VulkanBase::FramebufferResizeCallback(GLFWwindow* window, int, int)
{
	const auto base = static_cast<VulkanBase*>(glfwGetWindowUserPointer(window));
	base->framebufferResized = true;
}

VulkanBase::VulkanBase(GLFWwindow* w, const bool validation, const bool wireframe)
	: window(w)
	, validationEnabled(validation)
	, wireframeEnabled(wireframe)
{
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);

	CreateInstance();
	CreateSurface();
	PickPhysicalDevice();
	CreateLogicalDevice();
	CreateSpriteSetLayout();
	CreateSpriteBuffers();
	CreateSpriteDescriptorPool();
	CreateSpriteDescriptorSets();
	CreateSwapchain();
	CreateImageViews();
	CreateRenderPass();
	CreateGraphicsPipeline();
	CreateFramebuffers();
	CreateCommandPool();
	CreateCommandBuffers();
	CreateSyncObjects();

	/**
	 * Upload the fallback white texture and bind it so descriptor set
	 * binding 1 is never uninitialised before the first game texture is loaded.
	 */
	m_whiteTexture = CreateWhiteTexture();
	BindTexture(m_whiteTexture);
}

VulkanBase::~VulkanBase()
{
	vkDeviceWaitIdle(device);
	CleanupSwapchain();

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
		vkDestroyFence(device, inFlightFences[i], nullptr);
	}

	for (const VkSemaphore s : renderFinishedSemaphores)
	{
		vkDestroySemaphore(device, s, nullptr);
	}

	vkDestroyCommandPool(device, commandPool, nullptr);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkUnmapMemory(device, spriteBufferMemory[i]);
		vkDestroyBuffer(device, spriteBuffers[i], nullptr);
		vkFreeMemory(device, spriteBufferMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(device, spriteDescriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, spriteSetLayout, nullptr);

	DestroyTexture(m_whiteTexture);

	vkDestroyDevice(device, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
}

void VulkanBase::WaitIdle() const
{
	vkDeviceWaitIdle(device);
}

void VulkanBase::CreateInstance()
{
	if (validationEnabled)
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
				found = true;
				break;
			}
		}

		if (!found)
		{
			std::cout << "[Warning] Validation layer requested but not available. Are you missing the VulkanSDK?\n";
		}
	}

	VkApplicationInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	ai.pApplicationName = "WackyEngine";
	ai.apiVersion = VK_API_VERSION_1_3;

	uint32_t glfwExtCount = 0;
	const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
	std::vector exts(glfwExts, glfwExts + glfwExtCount);

	if (validationEnabled)
	{
		exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.pApplicationInfo = &ai;
	ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
	ci.ppEnabledExtensionNames = exts.data();

	if (validationEnabled)
	{
		ci.enabledLayerCount = 1;
		ci.ppEnabledLayerNames = &VALIDATION_LAYER;
	}

	VkCheck(vkCreateInstance(&ci, nullptr, &instance), "vkCreateInstance");
}

void VulkanBase::CreateSurface()
{
	VkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
}

void VulkanBase::PickPhysicalDevice()
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

			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);

			if (present)
			{
				presentFamily = i;
				foundP = true;
			}
		}

		if (foundG && foundP)
		{
			physicalDevice = pd;
			return;
		}
	}

	throw std::runtime_error("No suitable GPU found");
}

void VulkanBase::CreateLogicalDevice()
{
	constexpr float priority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> qcis;

	for (const uint32_t fam : { graphicsFamily, presentFamily })
	{
		bool already = false;

		for (const auto& q : qcis)
		{
			if (q.queueFamilyIndex == fam)
			{
				already = true;
				break;
			}
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

	/**
	 * shaderDrawParameters is required for SV_VertexID in Slang/HLSL shaders.
	 * fillModeNonSolid is required for wireframe rendering (editor builds only).
	 */
	VkPhysicalDeviceVulkan11Features features11{};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.shaderDrawParameters = VK_TRUE;

	VkPhysicalDeviceFeatures features{};
	features.fillModeNonSolid = wireframeEnabled ? VK_TRUE : VK_FALSE;

	const auto devExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

	VkDeviceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	ci.pNext = &features11;
	ci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
	ci.pQueueCreateInfos = qcis.data();
	ci.enabledExtensionCount = 1;
	ci.ppEnabledExtensionNames = &devExt;
	ci.pEnabledFeatures = &features;

	if (validationEnabled)
	{
		ci.enabledLayerCount = 1;
		ci.ppEnabledLayerNames = &VALIDATION_LAYER;
	}

	VkCheck(vkCreateDevice(physicalDevice, &ci, nullptr, &device), "vkCreateDevice");
	vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void VulkanBase::CreateCommandPool()
{
	VkCommandPoolCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	ci.queueFamilyIndex = graphicsFamily;
	VkCheck(vkCreateCommandPool(device, &ci, nullptr, &commandPool), "vkCreateCommandPool");
}

void VulkanBase::CreateSyncObjects()
{
	/**
	 * imageAvailableSemaphores and inFlightFences: one per frame in flight, indexed by currentFrame.
	 * renderFinishedSemaphores: one per swapchain image, indexed by imageIndex.
	 * vkQueuePresentKHR waits on renderFinished, and the spec only guarantees it is safe
	 * to reuse once vkAcquireNextImageKHR returns that same image index again.
	 */
	imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	renderFinishedSemaphores.resize(swapchainImages.size());
	inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

	constexpr VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkFenceCreateInfo fi{};
	fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
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

uint32_t VulkanBase::FindMemoryType(const uint32_t typeFilter, const VkMemoryPropertyFlags props) const
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
	{
		if (typeFilter & 1u << i && (memProps.memoryTypes[i].propertyFlags & props) == props)
		{
			return i;
		}
	}

	throw std::runtime_error("FindMemoryType: no suitable memory type found");
}

void VulkanBase::CreateSwapchain()
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

void VulkanBase::CreateImageViews()
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

void VulkanBase::CreateRenderPass()
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

void VulkanBase::CreateGraphicsPipeline()
{
	const auto vert = ReadFile("vert.spv");
	const auto frag = ReadFile("frag.spv");

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

	VkPushConstantRange pcRange{};
	pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcRange.offset = 0;
	pcRange.size = sizeof(CameraUniforms);

	VkPipelineLayoutCreateInfo layoutCI{};
	layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &spriteSetLayout;
	layoutCI.pushConstantRangeCount = 1;
	layoutCI.pPushConstantRanges = &pcRange;
	VkCheck(vkCreatePipelineLayout(device, &layoutCI, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = static_cast<float>(swapchainExtent.width);
	viewport.height = static_cast<float>(swapchainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = swapchainExtent;

	VkPipelineViewportStateCreateInfo vps{};
	vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vps.viewportCount = 1;
	vps.pViewports = &viewport;
	vps.scissorCount = 1;
	vps.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rast{};
	rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rast.polygonMode = wireframeEnabled ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	rast.cullMode = VK_CULL_MODE_NONE;
	rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rast.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState cba{};
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cba.colorBlendOp = VK_BLEND_OP_ADD;
	cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	cba.alphaBlendOp = VK_BLEND_OP_ADD;
	cba.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

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

void VulkanBase::CreateFramebuffers()
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

void VulkanBase::CreateCommandBuffers()
{
	commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

	VkCommandBufferAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ai.commandPool = commandPool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
	VkCheck(vkAllocateCommandBuffers(device, &ai, commandBuffers.data()), "vkAllocateCommandBuffers");
}

void VulkanBase::CleanupSwapchain()
{
	for (const VkFramebuffer fb : framebuffers)
	{
		vkDestroyFramebuffer(device, fb, nullptr);
	}

	for (const VkImageView iv : swapchainImageViews)
	{
		vkDestroyImageView(device, iv, nullptr);
	}

	vkDestroyPipeline(device, graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);

	framebuffers.clear();
	swapchainImageViews.clear();
	swapchainImages.clear();
}

bool VulkanBase::RecreateSwapchain()
{
	int w = 0;
	int h = 0;
	glfwGetFramebufferSize(window, &w, &h);

	if (w == 0 || h == 0)
	{
		return false;
	}

	vkDeviceWaitIdle(device);
	CleanupSwapchain();
	CreateSwapchain();
	CreateImageViews();

	/**
	 * renderFinishedSemaphores are sized per swapchain image.
	 * Recreate them in case the image count changed after recreation.
	 */
	for (const VkSemaphore s : renderFinishedSemaphores)
	{
		vkDestroySemaphore(device, s, nullptr);
	}

	renderFinishedSemaphores.clear();

	constexpr VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	renderFinishedSemaphores.resize(swapchainImages.size());

	for (size_t i = 0; i < swapchainImages.size(); i++)
	{
		VkCheck(vkCreateSemaphore(device, &si, nullptr, &renderFinishedSemaphores[i]), "vkCreateSemaphore renderFinished");
	}

	CreateRenderPass();
	CreateGraphicsPipeline();
	CreateFramebuffers();
	CreateCommandBuffers();
	return true;
}

void VulkanBase::CreateSpriteSetLayout()
{
	VkDescriptorSetLayoutBinding ssbo{};
	ssbo.binding = 0;
	ssbo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	ssbo.descriptorCount = 1;
	ssbo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutBinding sampler{};
	sampler.binding = 1;
	sampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sampler.descriptorCount = 1;
	sampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding bindings[] = { ssbo, sampler };

	VkDescriptorSetLayoutCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	ci.bindingCount = 2;
	ci.pBindings = bindings;
	VkCheck(vkCreateDescriptorSetLayout(device, &ci, nullptr, &spriteSetLayout), "vkCreateDescriptorSetLayout");
}

void VulkanBase::CreateSpriteBuffers()
{
	/**
	 * One host-visible, persistently-mapped buffer per frame in flight.
	 * HOST_COHERENT means writes are visible to the GPU without an explicit flush.
	 */
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

void VulkanBase::CreateSpriteDescriptorPool()
{
	VkDescriptorPoolSize poolSizes[2]{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

	VkDescriptorPoolCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	ci.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ci.poolSizeCount = 2;
	ci.pPoolSizes = poolSizes;
	VkCheck(vkCreateDescriptorPool(device, &ci, nullptr, &spriteDescriptorPool), "vkCreateDescriptorPool");
}

void VulkanBase::CreateSpriteDescriptorSets()
{
	std::vector layouts(MAX_FRAMES_IN_FLIGHT, spriteSetLayout);

	VkDescriptorSetAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ai.descriptorPool = spriteDescriptorPool;
	ai.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ai.pSetLayouts = layouts.data();

	spriteDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
	VkCheck(vkAllocateDescriptorSets(device, &ai, spriteDescriptorSets.data()), "vkAllocateDescriptorSets sprite");

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

VkCommandBuffer VulkanBase::BeginSingleTimeCommands() const
{
	VkCommandBufferAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ai.commandPool = commandPool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;

	VkCommandBuffer cmd;
	VkCheck(vkAllocateCommandBuffers(device, &ai, &cmd), "vkAllocateCommandBuffers single-time");

	VkCommandBufferBeginInfo bi{};
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkCheck(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer single-time");

	return cmd;
}

void VulkanBase::EndSingleTimeCommands(const VkCommandBuffer cmd) const
{
	VkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer single-time");

	VkSubmitInfo si{};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;

	VkCheck(vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit single-time");
	vkQueueWaitIdle(graphicsQueue);
	vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

void VulkanBase::CreateImage(const uint32_t w, const uint32_t h, const VkFormat fmt, const VkImageUsageFlags usage, VkImage& outImage, VkDeviceMemory& outMemory) const
{
	VkImageCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ci.imageType = VK_IMAGE_TYPE_2D;
	ci.format = fmt;
	ci.extent = { w, h, 1 };
	ci.mipLevels = 1;
	ci.arrayLayers = 1;
	ci.samples = VK_SAMPLE_COUNT_1_BIT;
	ci.tiling = VK_IMAGE_TILING_OPTIMAL;
	ci.usage = usage;
	ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkCheck(vkCreateImage(device, &ci, nullptr, &outImage), "vkCreateImage");

	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, outImage, &req);

	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkCheck(vkAllocateMemory(device, &mai, nullptr, &outMemory), "vkAllocateMemory image");

	vkBindImageMemory(device, outImage, outMemory, 0);
}

void VulkanBase::TransitionImageLayout(const VkImage image, const VkImageLayout oldLayout, const VkImageLayout newLayout) const
{
	const VkCommandBuffer cmd = BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags srcStage;
	VkPipelineStageFlags dstStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else
	{
		throw std::runtime_error("TransitionImageLayout: unsupported layout transition");
	}

	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	EndSingleTimeCommands(cmd);
}

void VulkanBase::CopyBufferToImage(const VkBuffer buffer, const VkImage image, const uint32_t w, const uint32_t h) const
{
	VkCommandBuffer cmd = BeginSingleTimeCommands();

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { w, h, 1 };

	vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	EndSingleTimeCommands(cmd);
}

VkImageView VulkanBase::CreateTextureImageView(const VkImage image) const
{
	VkImageViewCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ci.image = image;
	ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ci.format = VK_FORMAT_R8G8B8A8_SRGB;
	ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	ci.subresourceRange.levelCount = 1;
	ci.subresourceRange.layerCount = 1;

	VkImageView view;
	VkCheck(vkCreateImageView(device, &ci, nullptr, &view), "vkCreateImageView texture");
	return view;
}

VkSampler VulkanBase::CreateTextureSampler() const
{
	/**
	 * NEAREST filtering preserves the hard pixel edges of Aseprite exports.
	 * CLAMP_TO_EDGE prevents border bleed when UVs sit exactly on 0 or 1.
	 */
	VkSamplerCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	ci.magFilter = VK_FILTER_NEAREST;
	ci.minFilter = VK_FILTER_NEAREST;
	ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

	VkSampler sampler;
	VkCheck(vkCreateSampler(device, &ci, nullptr, &sampler), "vkCreateSampler");
	return sampler;
}

Texture VulkanBase::CreateWhiteTexture() const
{
	/**
	 * Upload a single opaque-white RGBA pixel.
	 * Acts as the default texture so binding 1 is never uninitialised.
	 */
	constexpr uint32_t white = 0xFFFFFFFF;
	constexpr VkDeviceSize size = sizeof(white);

	VkBuffer stagingBuf;
	VkDeviceMemory stagingMem;

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkCheck(vkCreateBuffer(device, &bci, nullptr, &stagingBuf), "vkCreateBuffer white staging");

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, stagingBuf, &req);

	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkCheck(vkAllocateMemory(device, &mai, nullptr, &stagingMem), "vkAllocateMemory white staging");
	vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

	void* dst;
	vkMapMemory(device, stagingMem, 0, size, 0, &dst);
	memcpy(dst, &white, sizeof(white));
	vkUnmapMemory(device, stagingMem);

	Texture tex;
	tex.width = 1;
	tex.height = 1;
	CreateImage(1, 1, VK_FORMAT_R8G8B8A8_SRGB,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		tex.image, tex.memory);

	TransitionImageLayout(tex.image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	CopyBufferToImage(stagingBuf, tex.image, 1, 1);
	TransitionImageLayout(tex.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkDestroyBuffer(device, stagingBuf, nullptr);
	vkFreeMemory(device, stagingMem, nullptr);

	tex.view = CreateTextureImageView(tex.image);
	tex.sampler = CreateTextureSampler();
	return tex;
}

Texture VulkanBase::LoadTexture(const std::string& path) const
{
	int w, h, ch;
	stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);

	if (!pixels)
	{
		throw std::runtime_error("LoadTexture: stbi_load failed: " + path);
	}

	const VkDeviceSize size = static_cast<VkDeviceSize>(w * h * 4);

	VkBuffer stagingBuf;
	VkDeviceMemory stagingMem;

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkCheck(vkCreateBuffer(device, &bci, nullptr, &stagingBuf), "vkCreateBuffer texture staging");

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, stagingBuf, &req);

	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkCheck(vkAllocateMemory(device, &mai, nullptr, &stagingMem), "vkAllocateMemory texture staging");
	vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

	void* dst;
	vkMapMemory(device, stagingMem, 0, size, 0, &dst);
	memcpy(dst, pixels, size);
	vkUnmapMemory(device, stagingMem);
	stbi_image_free(pixels);

	Texture tex;
	tex.width = w;
	tex.height = h;
	CreateImage(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		tex.image, tex.memory);

	TransitionImageLayout(tex.image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	CopyBufferToImage(stagingBuf, tex.image, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
	TransitionImageLayout(tex.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkDestroyBuffer(device, stagingBuf, nullptr);
	vkFreeMemory(device, stagingMem, nullptr);

	tex.view = CreateTextureImageView(tex.image);
	tex.sampler = CreateTextureSampler();
	return tex;
}

void VulkanBase::DestroyTexture(Texture& tex) const
{
	if (!tex.IsValid())
	{
		return;
	}

	vkDeviceWaitIdle(device);

	vkDestroySampler(device, tex.sampler, nullptr);
	vkDestroyImageView(device, tex.view, nullptr);
	vkDestroyImage(device, tex.image, nullptr);
	vkFreeMemory(device, tex.memory, nullptr);

	tex = {};
}

void VulkanBase::BindTexture(const Texture& tex) const
{
	/**
	 * Descriptor sets must not be updated while any command buffer that
	 * references them is pending. Wait for the device to drain first.
	 * BindTexture is a load-time / texture-switch operation, not a
	 * per-frame hot path, so this stall is acceptable.
	 */
	vkDeviceWaitIdle(device);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorImageInfo imgInfo{};
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imgInfo.imageView = tex.view;
		imgInfo.sampler = tex.sampler;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = spriteDescriptorSets[i];
		write.dstBinding = 1;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imgInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}
}

bool VulkanBase::BeginFrame(FrameContext& out)
{
	const VkFence fence = inFlightFences[currentFrame];
	vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

	const VkResult result = vkAcquireNextImageKHR(
		device,
		swapchain,
		UINT64_MAX,
		imageAvailableSemaphores[currentFrame],
		VK_NULL_HANDLE,
		&out.imageIndex
	);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		RecreateSwapchain();
		return false;
	}

	vkResetFences(device, 1, &fence);

	out.cmd = commandBuffers[currentFrame];
	vkResetCommandBuffer(out.cmd, 0);

	constexpr VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	vkBeginCommandBuffer(out.cmd, &bi);
	return true;
}

void VulkanBase::BeginRenderPass(const FrameContext& ctx, const SpriteList& scene) const
{
	/**
	 * Upload this frame's scene data into the matching SSBO slot.
	 * HOST_COHERENT memory requires no explicit flush.
	 */
	memcpy(spriteBufferMapped[currentFrame], &scene, sizeof(SpriteList));

	constexpr VkClearValue clear{ .color = {0.05f, 0.05f, 0.08f, 1.0f} };

	VkRenderPassBeginInfo rpbi{};
	rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpbi.renderPass = renderPass;
	rpbi.framebuffer = framebuffers[ctx.imageIndex];
	rpbi.renderArea.extent = swapchainExtent;
	rpbi.clearValueCount = 1;
	rpbi.pClearValues = &clear;
	vkCmdBeginRenderPass(ctx.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanBase::DrawSprites(const FrameContext& ctx, const SpriteList& scene, const CameraUniforms& cam) const
{
	vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

	vkCmdPushConstants(ctx.cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CameraUniforms), &cam);

	vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &spriteDescriptorSets[currentFrame], 0, nullptr);

	if (scene.count > 0)
	{
		vkCmdDraw(ctx.cmd, scene.count * 6, 1, 0, 0);
	}
}

void VulkanBase::EndRenderPass(const FrameContext& ctx)
{
	vkCmdEndRenderPass(ctx.cmd);
}

void VulkanBase::EndFrame(const FrameContext& ctx)
{
	vkEndCommandBuffer(ctx.cmd);

	/**
	 * imageAvailable semaphore: per frame-in-flight, safe because the fence above
	 * ensures this frame's previous acquire has already been waited on by submit.
	 *
	 * renderFinished semaphore: MUST be per swapchain image, indexed by imageIndex.
	 * vkQueuePresentKHR waits on this semaphore but provides no way to signal when
	 * it is done. The only guarantee of reuse safety is that vkAcquireNextImageKHR
	 * returning this imageIndex means the previous present for that image is complete.
	 */
	const VkSemaphore wait = imageAvailableSemaphores[currentFrame];
	const VkSemaphore signal = renderFinishedSemaphores[ctx.imageIndex];

	constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &wait;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &ctx.cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &signal;
	VkCheck(vkQueueSubmit(graphicsQueue, 1, &submit, inFlightFences[currentFrame]), "vkQueueSubmit");

	VkPresentInfoKHR present{};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &signal;
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &ctx.imageIndex;

	const VkResult presentResult = vkQueuePresentKHR(presentQueue, &present);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized)
	{
		framebufferResized = false;
		RecreateSwapchain();
	}

	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}