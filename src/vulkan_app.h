/**
 * VulkanApp —— Vulkan + GLFW 应用封装
 *
 * 封装了 Vulkan 实例、GLFW 窗口和主循环，
 * 提供简洁的接口供 main.cpp 调用。
 */
#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

// ============================================================================
// 全局常量
// ============================================================================
constexpr uint32_t WINDOW_WIDTH  = 800;
constexpr uint32_t WINDOW_HEIGHT = 600;
constexpr const char* APP_NAME   = "VulkanApp (MinGW + GLFW + GLM)";

// ============================================================================
// VulkanApp —— 主应用类
// ============================================================================
class VulkanApp final
{
public:
    VulkanApp();
    ~VulkanApp();

    // 禁止拷贝
    VulkanApp(const VulkanApp&) = delete;
    VulkanApp& operator=(const VulkanApp&) = delete;

    // 运行主循环（阻塞，直到窗口关闭）
    void run();

    // 获取内部对象（供后续扩展使用）
    VkInstance   vulkanInstance() const { return m_instance; }
    GLFWwindow*  window()         const { return m_window; }

private:
    // ---- Vulkan 相关 ----
    void createVulkanInstance();

    // ---- GLFW 相关 ----
    void createWindow();

    // ---- 成员变量 ----
    VkInstance  m_instance = VK_NULL_HANDLE;
    GLFWwindow* m_window   = nullptr;
};
