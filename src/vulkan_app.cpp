/**
 * VulkanApp 实现
 */
#include "vulkan_app.h"

#include <iostream>
#include <stdexcept>
#include <vector>

// ============================================================================
// 构造 / 析构
// ============================================================================

VulkanApp::VulkanApp()
{
    createWindow();
    createVulkanInstance();
    std::cout << "[VulkanApp] 初始化完成。" << std::endl;
}

VulkanApp::~VulkanApp()
{
    // 先销毁 Vulkan 实例，再销毁窗口
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        std::cout << "[Vulkan] 实例已销毁。" << std::endl;
    }

    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        std::cout << "[GLFW] 窗口已销毁。" << std::endl;
    }

    glfwTerminate();
    std::cout << "[GLFW] 已终止。" << std::endl;
}

// ============================================================================
// 主循环
// ============================================================================

void VulkanApp::run()
{
    std::cout << "[主循环] 按 ESC 退出..." << std::endl;

    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }
}

// ============================================================================
// GLFW 窗口创建
// ============================================================================

void VulkanApp::createWindow()
{
    std::cout << "[GLFW] 初始化 GLFW..." << std::endl;

    if (!glfwInit())
    {
        throw std::runtime_error("GLFW 初始化失败");
    }

    // 告知 GLFW 不要创建 OpenGL 上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(
        WINDOW_WIDTH, WINDOW_HEIGHT, APP_NAME, nullptr, nullptr);

    if (!m_window)
    {
        glfwTerminate();
        throw std::runtime_error("GLFW 窗口创建失败");
    }

    std::cout << "[GLFW] 窗口创建成功 (" << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << ")。" << std::endl;
}

// ============================================================================
// Vulkan 实例创建
// ============================================================================

void VulkanApp::createVulkanInstance()
{
    // --- 应用程序信息 ---
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = APP_NAME;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "No Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    // --- 实例创建信息 ---
    VkInstanceCreateInfo createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // --- 获取 GLFW 所需的 Vulkan 扩展 ---
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> enabledExtensions(
        glfwExtensions, glfwExtensions + glfwExtensionCount);

    std::cout << "[Vulkan] GLFW 要求 " << glfwExtensionCount << " 个实例扩展：" << std::endl;
    for (uint32_t i = 0; i < glfwExtensionCount; ++i)
    {
        std::cout << "         " << glfwExtensions[i] << std::endl;
    }

    createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    // 不使用验证层（开发时可启用）
    createInfo.enabledLayerCount    = 0;
    createInfo.ppEnabledLayerNames  = nullptr;

    // --- 创建实例 ---
    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(
            "vkCreateInstance 失败，错误码: " +
            std::to_string(static_cast<int>(result)));
    }

    std::cout << "[Vulkan] 实例创建成功。" << std::endl;
}
