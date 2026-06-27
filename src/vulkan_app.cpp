/**
 * vulkan_app.cpp —— VulkanApp 主应用实现
 *
 * 整合所有游戏模块的主循环逻辑。
 */
#include "vulkan_app.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <sstream>

// ============================================================================
// 构造 / 析构
// ============================================================================

VulkanApp::VulkanApp()
{
    createWindow();
    createVulkanInstance();
    initGameModules();
    std::cout << "[VulkanApp] 初始化完成。" << std::endl;
}

VulkanApp::~VulkanApp()
{
    // 先销毁渲染引擎（等待 GPU 空闲）
    m_renderEngine.reset();

    // 销毁世界（释放所有区块）
    m_world.reset();

    // 销毁 Vulkan 实例
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        std::cout << "[Vulkan] 实例已销毁。" << std::endl;
    }

    // 销毁窗口
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
// 初始化
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
        m_width, m_height, APP_NAME, nullptr, nullptr);

    if (!m_window)
    {
        glfwTerminate();
        throw std::runtime_error("GLFW 窗口创建失败");
    }

    // 设置用户指针（用于回调）
    glfwSetWindowUserPointer(m_window, this);

    // 设置回调
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
    glfwSetCursorPosCallback(m_window, onMouseMove);
    glfwSetMouseButtonCallback(m_window, onMouseButton);
    glfwSetKeyCallback(m_window, onKey);

    // 捕获鼠标
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    std::cout << "[GLFW] 窗口创建成功 (" << m_width << "x" << m_height << ")。" << std::endl;
}

void VulkanApp::createVulkanInstance()
{
    // --- 应用程序信息 ---
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = APP_NAME;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "VoxelEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

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

    // 不使用验证层
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

void VulkanApp::initGameModules()
{
    // 创建渲染引擎（需要窗口和 Vulkan 实例）
    m_renderEngine = std::make_unique<RenderEngine>(m_window, m_instance);
    m_renderEngine->init();

    // 创建世界（种子 42，加载半径 10 区块，垂直 6 区块）
    m_world = std::make_unique<World>(42, 10, 6);

    // 创建玩家
    m_player = std::make_unique<Player>();
    m_player->setPosition(glm::vec3(0.0f, 80.0f, 0.0f));

    // 同步生成初始区块（半径 4 区块 = 64×64 方块的地面区域）
    m_world->generateInitialChunks(m_player->getPosition(), 4);

    // 创建优化管理器
    m_optimizer = std::make_unique<OptimizationManager>();

    // 设置摄像机宽高比
    m_player->getCamera().setAspectRatio(
        static_cast<float>(m_width) / static_cast<float>(m_height));

    std::cout << "[VulkanApp] 游戏模块初始化完成。" << std::endl;
}

// ============================================================================
// 主循环
// ============================================================================

void VulkanApp::run()
{
    std::cout << "[主循环] 开始运行。" << std::endl;

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(m_window))
    {
        // ---- 计算时间 ----
        auto currentTime = std::chrono::high_resolution_clock::now();
        m_deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (m_deltaTime > 0.1f) m_deltaTime = 0.1f;

        // ---- FPS 统计 ----
        m_frameCount++;
        m_fpsTimer += m_deltaTime;
        if (m_fpsTimer >= 1.0f)
        {
            m_fps = static_cast<float>(m_frameCount) / m_fpsTimer;
            m_frameCount = 0;
            m_fpsTimer = 0.0f;
            updateWindowTitle();
        }

        // ---- 处理事件 ----
        glfwPollEvents();

        // ---- 更新玩家 (含碰撞检测) ----
        if (m_player->isMouseCaptured())
        {
            m_player->update(m_deltaTime, m_window);
        }

        // ---- 更新世界（加载/卸载区块）----
        m_world->update(m_player->getPosition());

        // ---- 视锥体剔除 ----
        auto frustumPlanes = m_player->getCamera().getFrustumPlanes();
        std::vector<std::shared_ptr<Chunk>> visibleChunks;
        m_world->getVisibleChunks(frustumPlanes, visibleChunks,
                                  m_player->getPosition());
        m_lastVisibleChunks = visibleChunks.size();

        // ---- 合批（屏幕空间动态 LOD）----
        std::array<BatchData, 3> batches;

        // 计算 View-Projection 矩阵用于屏幕空间 LOD
        auto& cam = m_player->getCamera();
        glm::mat4 viewProj = cam.getProjectionMatrix() * cam.getViewMatrix();

        m_optimizer->processChunksScreenSpace(
            visibleChunks, viewProj, m_width, m_height, batches);

        // ---- 统计 ----
        m_lastChunkCount  = m_world->getLoadedChunkCount();
        m_lastDrawCalls   = m_optimizer->getDrawCallCount();
        m_lastVertices    = m_optimizer->getTotalVertices();
        m_lastTriangles   = m_optimizer->getTotalTriangles();

        // ---- 渲染 ----
        m_renderEngine->render(m_player->getCamera(), batches, "");

        // ---- 窗口大小变化 ----
        if (m_framebufferResized)
        {
            int newWidth, newHeight;
            glfwGetFramebufferSize(m_window, &newWidth, &newHeight);
            if (newWidth > 0 && newHeight > 0)
            {
                m_width = newWidth;
                m_height = newHeight;
                m_renderEngine->onResize();
                m_player->getCamera().setAspectRatio(
                    static_cast<float>(newWidth) / static_cast<float>(newHeight));
            }
            m_framebufferResized = false;
        }
    }

    m_renderEngine->waitIdle();
    std::cout << "[主循环] 结束。" << std::endl;
}

// ============================================================================
// 窗口标题更新
// ============================================================================

void VulkanApp::updateWindowTitle()
{
    std::ostringstream title;
    title << "VoxelWorld - "
          << "FPS: " << static_cast<int>(m_fps)
          << " | 区块: " << m_lastChunkCount
          << " (可见: " << m_lastVisibleChunks << ")"
          << " | Draw Calls: " << m_lastDrawCalls
          << " | 顶点: " << m_lastVertices
          << " | 三角面: " << m_lastTriangles
          << " | 待处理: " << m_world->getTotalPendingTasks();

    glfwSetWindowTitle(m_window, title.str().c_str());
}

// ============================================================================
// GLFW 回调
// ============================================================================

void VulkanApp::onFramebufferResize(GLFWwindow* window, int width, int height)
{
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    if (app)
    {
        app->m_framebufferResized = true;
        app->m_width  = width;
        app->m_height = height;
    }
}

void VulkanApp::onMouseMove(GLFWwindow* window, double xpos, double ypos)
{
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    if (app && app->m_player)
    {
        app->m_player->onMouseMove(xpos, ypos);
    }
}

void VulkanApp::onMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    if (app && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        // 左键点击激活鼠标捕获
        if (!app->m_player->isMouseCaptured())
        {
            app->m_player->setMouseCaptured(true);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
}

void VulkanApp::onKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_player) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        if (app->m_player->isMouseCaptured())
        {
            app->m_player->setMouseCaptured(false);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        else
        {
            app->m_player->setMouseCaptured(true);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
}
