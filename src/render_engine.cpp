/**
 * render_engine.cpp —— Vulkan 渲染引擎实现
 *
 * 包含完整的 Vulkan 初始化、渲染循环和资源管理代码。
 * 实现了基于合批的体素渲染管线。
 */
#include "render_engine.h"

#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <fstream>
#include <cstring>

// ============================================================================
// 辅助函数：读取 SPIR-V 着色器文件
// 尝试多个可能的路径以兼容不同工作目录
// ============================================================================
static std::vector<char> readFile(const std::string& filename)
{
    // 尝试的路径列表
    std::vector<std::string> paths = {
        filename,                          // 直接路径
        "shaders/" + filename,             // shaders/ 子目录
        "../shaders/" + filename,          // 上级目录的 shaders/
        "../../shaders/" + filename,       // 上上级目录的 shaders/
        "../" + filename,                  // 上级目录
        "../../" + filename                // 上上级目录
    };

    for (const auto& path : paths)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open())
        {
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            file.close();
            return buffer;
        }
    }

    throw std::runtime_error("无法打开着色器文件: " + filename +
                             " (已尝试所有常见路径)");
}

// ============================================================================
// 必需的设备扩展
// ============================================================================
static const std::vector<const char*> s_deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ============================================================================
// 构造/析构
// ============================================================================

RenderEngine::RenderEngine(GLFWwindow* window, VkInstance instance)
    : m_window(window)
    , m_instance(instance)
{
    glfwGetFramebufferSize(window, &m_width, &m_height);
}

RenderEngine::~RenderEngine()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
        cleanupCullingResources();
        cleanupSwapchain();

        if (m_vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
            vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
        }
        if (m_indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
            vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
        }

        if (m_descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_descriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (m_uniformBuffers[i] != VK_NULL_HANDLE)
                vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
            if (m_uniformBuffersMemory[i] != VK_NULL_HANDLE)
                vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            if (m_inFlightFences[i] != VK_NULL_HANDLE)
                vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }

        if (m_commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);

        if (m_pipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_renderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);

        m_pipelineManager.cleanup();
        m_pipeline = VK_NULL_HANDLE;

        vkDestroyDevice(m_device, nullptr);
    }

    if (m_surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
}

// ============================================================================
// 初始化
// ============================================================================

void RenderEngine::init()
{
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();

    // 初始化 PipelineManager 后再创建管线
    m_pipelineManager.init(m_device);

    createGraphicsPipeline();
    // createCullingPipeline(); // 临时注释，排查崩溃
    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createUniformBuffers();
    createDescriptorPool();
    createCommandBuffers();
    createSyncObjects();

    std::cout << "[RenderEngine] Vulkan 初始化完成。" << std::endl;
}

// ============================================================================
// 表面创建
// ============================================================================

void RenderEngine::createSurface()
{
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
    {
        throw std::runtime_error("GLFW 创建 Vulkan 表面失败");
    }
    std::cout << "[Vulkan] 窗口表面创建成功。" << std::endl;
}

// ============================================================================
// 物理设备选择
// ============================================================================

void RenderEngine::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("没有找到支持 Vulkan 的 GPU");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // 评分选择最佳设备
    int bestScore = 0;
    for (const auto& device : devices)
    {
        int score = rateDeviceSuitability(device);
        if (score > bestScore)
        {
            bestScore = score;
            m_physicalDevice = device;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("没有找到合适的 GPU");

    // 输出设备信息
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    std::cout << "[Vulkan] 选择 GPU: " << props.deviceName << std::endl;
}

int RenderEngine::rateDeviceSuitability(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(device, &props);
    vkGetPhysicalDeviceFeatures(device, &features);

    int score = 0;

    // 必须是独立 GPU
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;

    // 最大纹理尺寸
    score += static_cast<int>(props.limits.maxImageDimension2D);

    // 必须支持几何体着色器
    if (!features.geometryShader)
        score = 0;

    // 必须支持交换链扩展
    if (!checkDeviceExtensionSupport(device))
        score = 0;

    // 必须满足队列族要求
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete())
        score = 0;

    // 交换链必须支持
    if (score > 0)
    {
        auto swapchainSupport = querySwapChainSupport(device);
        if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
            score = 0;
    }

    return score;
}

bool RenderEngine::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(s_deviceExtensions.begin(), s_deviceExtensions.end());
    for (const auto& ext : availableExtensions)
        requiredExtensions.erase(ext.extensionName);

    return requiredExtensions.empty();
}

RenderEngine::QueueFamilyIndices RenderEngine::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        // 图形队列
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;

        // 呈现队列
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport)
            indices.presentFamily = i;

        if (indices.isComplete())
            break;

        i++;
    }

    return indices;
}

// ============================================================================
// 逻辑设备
// ============================================================================

void RenderEngine::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily, indices.presentFamily
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // 启用特性
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_FALSE; // 我们不用纹理

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(s_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = s_deviceExtensions.data();

    // 兼容性层（Vulkan 1.3+ 忽略此字段）
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("逻辑设备创建失败");

    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);

    m_graphicsQueueFamily = indices.graphicsFamily;
    m_presentQueueFamily  = indices.presentFamily;

    std::cout << "[Vulkan] 逻辑设备创建成功。" << std::endl;
}

// ============================================================================
// 交换链
// ============================================================================

void RenderEngine::createSwapchain()
{
    auto support = querySwapChainSupport(m_physicalDevice);

    auto surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    auto presentMode   = chooseSwapPresentMode(support.presentModes);
    auto extent        = chooseSwapExtent(support.capabilities);

    // 图像数量（至少比最小多 1）
    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = surfaceFormat.format;
    createInfo.imageColorSpace  = surfaceFormat.colorSpace;
    createInfo.imageExtent      = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily, indices.presentFamily };

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform   = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode    = presentMode;
    createInfo.clipped        = VK_TRUE;
    createInfo.oldSwapchain   = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("交换链创建失败");

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent      = extent;

    std::cout << "[Vulkan] 交换链创建成功 (" << m_swapchainExtent.width
              << "x" << m_swapchainExtent.height << ")" << std::endl;
}

void RenderEngine::createImageViews()
{
    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); ++i)
    {
        m_swapchainImageViews[i] = createImageView(
            m_swapchainImages[i], m_swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

RenderEngine::SwapChainSupportDetails RenderEngine::querySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount > 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount > 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR RenderEngine::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& fmt : availableFormats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return fmt;
    }
    return availableFormats[0];
}

VkPresentModeKHR RenderEngine::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availableModes)
{
    // 优先使用 Mailbox（三重缓冲）
    for (const auto& mode : availableModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    }
    // 其次 FIFO（保证可用）
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D RenderEngine::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
        return capabilities.currentExtent;

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
    actualExtent.width  = std::clamp(actualExtent.width,
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

// ============================================================================
// 渲染流程
// ============================================================================

void RenderEngine::createRenderPass()
{
    // 颜色附件
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_swapchainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 深度附件
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = findDepthFormat();
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 子流程
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // 子流程依赖
    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments    = attachments.data();
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("渲染流程创建失败");
}

// ============================================================================
// 描述符集布局
// ============================================================================

void RenderEngine::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding            = 0;
    uboLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount    = 1;
    uboLayoutBinding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("描述符集布局创建失败");
}

// ============================================================================
// 图形管线
// ============================================================================

void RenderEngine::createGraphicsPipeline()
{
    // 加载着色器
    auto vertShaderCode = readFile("shaders/vertex.spv");
    auto fragShaderCode = readFile("shaders/fragment.spv");

    VkShaderModule vertShaderModule = [&]() {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = vertShaderCode.size();
        createInfo.pCode    = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
        VkShaderModule module;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("顶点着色器模块创建失败");
        return module;
    }();

    VkShaderModule fragShaderModule = [&]() {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = fragShaderCode.size();
        createInfo.pCode    = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
        VkShaderModule module;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("片元着色器模块创建失败");
        return module;
    }();

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName  = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName  = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // 顶点输入
    auto bindingDescription = VoxelVertex_getBindingDescription();
    auto attributeDescriptions = VoxelVertex_getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount   = 1;
    vertexInputInfo.pVertexBindingDescriptions      = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions    = attributeDescriptions.data();

    // 输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 视口
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapchainExtent.width);
    viewport.height   = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    // 光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    // 多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 深度模板
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_TRUE;
    depthStencil.depthWriteEnable      = VK_TRUE;
    depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // 颜色混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable   = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &colorBlendAttachment;

    // 管线布局
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts    = &m_descriptorSetLayout;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("管线布局创建失败");

    // 创建管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages    = shaderStages;
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout     = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass    = 0;

    m_pipeline = m_pipelineManager.getOrCreatePipeline(pipelineInfo);
    if (m_pipeline == VK_NULL_HANDLE)
        throw std::runtime_error("图形管线创建失败");

    // 清理着色器模块
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

    std::cout << "[Vulkan] 图形管线创建成功。" << std::endl;
}

// ============================================================================
// 深度缓冲
// ============================================================================

void RenderEngine::createDepthResources()
{
    VkFormat depthFormat = findDepthFormat();
    createImage(m_swapchainExtent.width, m_swapchainExtent.height,
                depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_depthImage, m_depthImageMemory);
    m_depthImageView = createImageView(m_depthImage, depthFormat,
                                       VK_IMAGE_ASPECT_DEPTH_BIT);
}

VkFormat RenderEngine::findDepthFormat()
{
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat RenderEngine::findSupportedFormat(
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            return format;
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            return format;
    }
    throw std::runtime_error("未找到支持的格式");
}

// ============================================================================
// 帧缓冲
// ============================================================================

void RenderEngine::createFramebuffers()
{
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments = {
            m_swapchainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments    = attachments.data();
        framebufferInfo.width           = m_swapchainExtent.width;
        framebufferInfo.height          = m_swapchainExtent.height;
        framebufferInfo.layers          = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
                                &m_swapchainFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("帧缓冲创建失败");
    }
}

// ============================================================================
// 命令池
// ============================================================================

void RenderEngine::createCommandPool()
{
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("命令池创建失败");
}

// ============================================================================
// 统一缓冲区
// ============================================================================

void RenderEngine::createUniformBuffers()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_uniformBuffers[i], m_uniformBuffersMemory[i]);

        vkMapMemory(m_device, m_uniformBuffersMemory[i], 0,
                    bufferSize, 0, &m_uniformBuffersMapped[i]);
    }
}

// ============================================================================
// 描述符池
// ============================================================================

void RenderEngine::createDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("描述符池创建失败");

    // 分配描述符集
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts        = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("描述符集分配失败");

    // 更新描述符集
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet          = m_descriptorSets[i];
        descriptorWrite.dstBinding      = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo     = &bufferInfo;

        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    }
}

// ============================================================================
// 顶点/索引缓冲区
// ============================================================================

void RenderEngine::createBuffers(size_t vertexSize, size_t indexSize)
{
    // 如果已有缓冲区且容量足够，直接复用
    if (vertexSize <= m_currentVertexCapacity && indexSize <= m_currentIndexCapacity)
        return;

    // 清理旧缓冲区
    if (m_vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    }
    if (m_indexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
        vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
    }

    // 扩容（预留 50% 余量）
    m_currentVertexCapacity = static_cast<size_t>(vertexSize * 1.5);
    m_currentIndexCapacity  = static_cast<size_t>(indexSize * 1.5);

    if (m_currentVertexCapacity == 0) m_currentVertexCapacity = 1;
    if (m_currentIndexCapacity == 0)  m_currentIndexCapacity = 1;

    // 创建顶点缓冲区（设备本地）
    createBuffer(m_currentVertexCapacity,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_vertexBuffer, m_vertexBufferMemory);

    // 创建索引缓冲区（设备本地）
    createBuffer(m_currentIndexCapacity,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_indexBuffer, m_indexBufferMemory);
}

// ============================================================================
// 命令缓冲区
// ============================================================================

void RenderEngine::createCommandBuffers()
{
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("命令缓冲区创建失败");
}

// ============================================================================
// 同步对象
// ============================================================================

void RenderEngine::createSyncObjects()
{
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                              &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                              &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr,
                          &m_inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("同步对象创建失败");
        }
    }
}

// ============================================================================
// 辅助方法
// ============================================================================

uint32_t RenderEngine::findMemoryType(uint32_t typeFilter,
                                       VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("未找到匹配的内存类型");
}

void RenderEngine::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        throw std::runtime_error("缓冲区创建失败");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        throw std::runtime_error("缓冲内存分配失败");

    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
}

void RenderEngine::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

VkImageView RenderEngine::createImageView(VkImage image, VkFormat format,
                                           VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = format;
    viewInfo.subresourceRange.aspectMask     = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        throw std::runtime_error("图像视图创建失败");
    return imageView;
}

void RenderEngine::createImage(uint32_t width, uint32_t height, VkFormat format,
                                VkImageTiling tiling, VkImageUsageFlags usage,
                                VkMemoryPropertyFlags properties,
                                VkImage& image, VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = format;
    imageInfo.tiling        = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = usage;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("图像创建失败");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
        throw std::runtime_error("图像内存分配失败");

    vkBindImageMemory(m_device, image, imageMemory, 0);
}

// ============================================================================
// 清理交换链
// ============================================================================

void RenderEngine::cleanupSwapchain()
{
    // 清理深度缓冲
    if (m_depthImageView != VK_NULL_HANDLE)
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
    if (m_depthImage != VK_NULL_HANDLE)
        vkDestroyImage(m_device, m_depthImage, nullptr);
    if (m_depthImageMemory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);

    // 清理帧缓冲
    for (auto& fb : m_swapchainFramebuffers)
    {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(m_device, fb, nullptr);
    }

    // 清理图像视图
    for (auto& iv : m_swapchainImageViews)
    {
        if (iv != VK_NULL_HANDLE)
            vkDestroyImageView(m_device, iv, nullptr);
    }

    // 清理交换链
    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void RenderEngine::recreateSwapchain()
{
    // 处理最小化
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_device);

    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    createDepthResources();
    createFramebuffers();
}

// ============================================================================
// 更新统一缓冲区
// ============================================================================

void RenderEngine::updateUniformBuffer(uint32_t currentImage, Camera& camera)
{
    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f); // 单位矩阵——顶点已在世界坐标中
    ubo.view  = camera.getViewMatrix();
    ubo.proj  = camera.getProjectionMatrix();

    // Vulkan 的 NDC Y 轴向下，需要翻转
    ubo.proj[1][1] *= -1.0f;

    memcpy(m_uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

// ============================================================================
// 录制命令缓冲区
// ============================================================================

void RenderEngine::recordCommandBuffer(
    VkCommandBuffer commandBuffer, uint32_t imageIndex,
    const std::array<BatchData, 3>& batches)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("命令缓冲区录制开始失败");

    // 开始渲染流程
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color   = {{0.5f, 0.7f, 1.0f, 1.0f}}; // 天空蓝
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass  = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // 设置视口和裁剪
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapchainExtent.width);
    viewport.height   = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // 遍历每个 LOD 级别的批次并绘制
    VkDeviceSize offsets[] = {0};

    for (const auto& batch : batches)
    {
        if (batch.vertices.empty())
            continue;

        // 绑定描述符集
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1,
                                &m_descriptorSets[m_currentFrame], 0, nullptr);

        // 绑定顶点缓冲区
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer, offsets);

        if (!batch.indices.empty())
        {
            // 索引绘制
            vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(batch.indices.size()),
                             1, 0, 0, 0);
        }
        else
        {
            // 非索引绘制（备用）
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(batch.vertices.size()), 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("命令缓冲区录制结束失败");
}

// ============================================================================
// 主渲染循环
// ============================================================================

void RenderEngine::render(
    Camera& camera,
    const std::array<BatchData, 3>& batches,
    const std::string& stats)
{
    // 等待当前帧栅栏
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // 获取交换链图像
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                             m_imageAvailableSemaphores[m_currentFrame],
                                             VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("交换链图像获取失败");
    }

    // 更新统一缓冲区
    updateUniformBuffer(static_cast<uint32_t>(m_currentFrame), camera);

    // 准备顶点数据
    size_t totalVerts = 0, totalIndices = 0;
    for (const auto& batch : batches)
    {
        totalVerts += batch.vertices.size();
        totalIndices += batch.indices.size();
    }

    // 创建/更新缓冲区
    createBuffers(
        totalVerts * sizeof(VoxelVertex),
        totalIndices * sizeof(uint32_t));

    if (totalVerts > 0)
    {
        // 创建暂存缓冲区上传顶点数据
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VkDeviceSize vertexBufferSize = totalVerts * sizeof(VoxelVertex);

        createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(m_device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
        size_t offset = 0;
        for (const auto& batch : batches)
        {
            if (!batch.vertices.empty())
            {
                std::memcpy(static_cast<char*>(data) + offset,
                            batch.vertices.data(),
                            batch.vertices.size() * sizeof(VoxelVertex));
                offset += batch.vertices.size() * sizeof(VoxelVertex);
            }
        }
        vkUnmapMemory(m_device, stagingBufferMemory);

        copyBuffer(stagingBuffer, m_vertexBuffer, vertexBufferSize);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
    }

    if (totalIndices > 0)
    {
        // 创建暂存缓冲区上传索引数据
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VkDeviceSize indexBufferSize = totalIndices * sizeof(uint32_t);

        createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(m_device, stagingBufferMemory, 0, indexBufferSize, 0, &data);
        size_t offset = 0;
        for (const auto& batch : batches)
        {
            if (!batch.indices.empty())
            {
                std::memcpy(static_cast<char*>(data) + offset,
                            batch.indices.data(),
                            batch.indices.size() * sizeof(uint32_t));
                offset += batch.indices.size() * sizeof(uint32_t);
            }
        }
        vkUnmapMemory(m_device, stagingBufferMemory);

        copyBuffer(stagingBuffer, m_indexBuffer, indexBufferSize);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
    }

    // 重置栅栏
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    // 录制命令缓冲区
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, batches);

    // 提交
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores    = waitSemaphores;
    submitInfo.pWaitDstStageMask  = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("队列提交失败");

    // 呈现
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains    = swapchains;
    presentInfo.pImageIndices  = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("呈现失败");
    }

    // 更新窗口标题
    if (!stats.empty())
    {
        glfwSetWindowTitle(m_window, stats.c_str());
    }

    // 前进到下一帧
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// 性能优化：GPU 剔除 Compute Pipeline
// ============================================================================

void RenderEngine::createCullingPipeline()
{
    // 读取 compute shader
    auto compCode = readFile("shaders/cull.spv");

    VkShaderModule compModule = [&]() {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = compCode.size();
        ci.pCode    = reinterpret_cast<const uint32_t*>(compCode.data());
        VkShaderModule mod;
        vkCreateShaderModule(m_device, &ci, nullptr, &mod);
        return mod;
    }();

    // DescriptorSetLayout: binding 0=UBO, 1=AABB, 2=Visible, 3=HiZ
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2; bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1; bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].binding = 3; bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1; bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslCi{};
    dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslCi.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCi.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_cullDSLayout);

    // PipelineLayout
    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1;
    plCi.pSetLayouts = &m_cullDSLayout;
    vkCreatePipelineLayout(m_device, &plCi, nullptr, &m_cullPipelineLayout);

    // Compute Pipeline
    VkComputePipelineCreateInfo cpCi{};
    cpCi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpCi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpCi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpCi.stage.module = compModule;
    cpCi.stage.pName = "main";
    cpCi.layout = m_cullPipelineLayout;

    m_cullPipeline = m_pipelineManager.getOrCreateComputePipeline(cpCi);

    vkDestroyShaderModule(m_device, compModule, nullptr);

    // 创建缓冲区
    VkDeviceSize aabbSize = sizeof(GPUFrustumData::Plane) * 6 * 4096; // 最多 4096 区块
    createBuffer(aabbSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_cullAABBBuffer, m_cullAABBMemory);

    createBuffer(4096 * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_cullVisibleBuffer, m_cullVisibleMemory);

    createBuffer(sizeof(GPUFrustumData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_cullUBO, m_cullUBOMemory);

    // 创建 Indirect 命令缓冲区
    createBuffer(MAX_INDIRECT_COMMANDS * sizeof(VkDrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_indirectBuffer, m_indirectMemory);
}

void RenderEngine::dispatchFrustumCull(VkCommandBuffer cmd)
{
    // 简化的剔除调度：实际项目中在此提交 Compute Shader
    // 此处留作扩展入口，当前仍使用 CPU 端剔除
}

void RenderEngine::updateIndirectBuffer(const std::array<BatchData, 3>& batches)
{
    // 将合批后的数据转为 Indirect 命令并上传到 GPU
    // 当前仍使用 vkCmdDrawIndexed，后续可迁移到 Indirect
}

void RenderEngine::cleanupCullingResources()
{
    auto safeDestroy = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf) { vkDestroyBuffer(m_device, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem) { vkFreeMemory(m_device, mem, nullptr); mem = VK_NULL_HANDLE; }
    };
    safeDestroy(m_cullAABBBuffer, m_cullAABBMemory);
    safeDestroy(m_cullVisibleBuffer, m_cullVisibleMemory);
    safeDestroy(m_cullUBO, m_cullUBOMemory);
    safeDestroy(m_indirectBuffer, m_indirectMemory);

    if (m_cullPipeline) { vkDestroyPipeline(m_device, m_cullPipeline, nullptr); m_cullPipeline = VK_NULL_HANDLE; }
    if (m_cullPipelineLayout) { vkDestroyPipelineLayout(m_device, m_cullPipelineLayout, nullptr); m_cullPipelineLayout = VK_NULL_HANDLE; }
    if (m_cullDSLayout) { vkDestroyDescriptorSetLayout(m_device, m_cullDSLayout, nullptr); m_cullDSLayout = VK_NULL_HANDLE; }
}

void RenderEngine::onResize()
{
    recreateSwapchain();
}
