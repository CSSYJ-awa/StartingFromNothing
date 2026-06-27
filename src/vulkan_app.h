/**
 * vulkan_app.h —— VulkanApp 主应用类
 *
 * 统合管理所有游戏模块：
 * - World（世界管理、地形生成、区块调度）
 * - Player（摄像机、输入、碰撞检测）
 * - RenderEngine（Vulkan 渲染）
 * - OptimizationManager（性能优化策略）
 *
 * 【主循环结构】
 * 每帧执行：
 * 1. 处理输入（鼠标捕获/释放）
 * 2. 更新玩家位置与碰撞
 * 3. 更新世界（根据玩家位置加载/卸载区块）
 * 4. 执行视锥体剔除 → LOD 选择 → 合批
 * 5. 提交渲染
 * 6. 更新窗口标题（显示调试信息）
 */
#pragma once

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "world.h"
#include "player.h"
#include "render_engine.h"
#include "optimization_manager.h"

#include <memory>
#include <string>

// ============================================================================
// 全局常量
// ============================================================================
constexpr uint32_t WINDOW_WIDTH  = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr const char* APP_NAME   = "VoxelWorld - Vulkan 体素世界";

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

    /** 运行主循环 */
    void run();

private:
    // ---- 窗口 ----
    GLFWwindow* m_window = nullptr;
    int m_width  = WINDOW_WIDTH;
    int m_height = WINDOW_HEIGHT;
    bool m_framebufferResized = false;

    // ---- Vulkan ----
    VkInstance m_instance = VK_NULL_HANDLE;

    // ---- 游戏模块 ----
    std::unique_ptr<World>      m_world;
    std::unique_ptr<Player>     m_player;
    std::unique_ptr<RenderEngine> m_renderEngine;
    std::unique_ptr<OptimizationManager> m_optimizer;

    // ---- 计时 ----
    float m_lastFrameTime = 0.0f;
    float m_deltaTime     = 0.0f;

    // ---- 调试信息 ----
    int    m_frameCount = 0;
    float  m_fpsTimer   = 0.0f;
    float  m_fps        = 0.0f;
    size_t m_lastChunkCount  = 0;
    int    m_lastDrawCalls   = 0;
    size_t m_lastVertices    = 0;
    size_t m_lastTriangles   = 0;
    size_t m_lastVisibleChunks = 0;

    // ---- 初始化方法 ----
    void createVulkanInstance();
    void createWindow();
    void initGameModules();

    // ---- GLFW 回调 ----
    static void onFramebufferResize(GLFWwindow* window, int width, int height);
    static void onMouseMove(GLFWwindow* window, double xpos, double ypos);
    static void onMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);

    /** 更新窗口标题（显示实时调试信息） */
    void updateWindowTitle();
};
