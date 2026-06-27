/**
 * render_engine.h —— Vulkan 渲染引擎
 *
 * 封装所有 Vulkan 渲染相关的操作，包括：
 * 1. 设备创建与初始化
 * 2. 交换链与帧缓冲
 * 3. 图形管线（支持 LOD 级别切换）
 * 4. 顶点/索引缓冲区管理
 * 5. 统一缓冲区（MVP 矩阵）
 * 6. 描述符与 Pipeline Layout
 * 7. 命令缓冲区录制与提交
 * 8. 同步对象（信号量、栅栏）
 *
 * 【渲染流程】
 * 每帧：
 * 1. 等待栅栏 → 获取交换链图像
 * 2. 更新统一缓冲区（摄像机 MVP）
 * 3. 更新顶点缓冲区（批量上传区块网格）
 * 4. 录制命令缓冲区
 * 5. 提交队列 → 呈现
 * 6. 更新窗口标题（显示调试信息）
 */
#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "camera.h"
#include "optimization_manager.h"
#include "pipeline_manager.h"
#include "perf_integration.h"

#include <vector>
#include <array>
#include <memory>
#include <string>
#include <glm/glm.hpp>

// ============================================================================
// 帧同步最大并发帧数
// ============================================================================
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// ============================================================================
// UniformBufferObject —— 统一缓冲区数据（用于 MVP 矩阵）
// ============================================================================
struct UniformBufferObject
{
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

// ============================================================================
// RenderEngine —— 渲染引擎
// ============================================================================
class RenderEngine
{
public:
    RenderEngine(GLFWwindow* window, VkInstance instance);
    ~RenderEngine();

    // 禁止拷贝
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    /**
     * 初始化所有 Vulkan 资源
     */
    void init();

    /**
     * 渲染一帧
     * @param camera 摄像机（用于获取 MVP）
     * @param batches 合批后的渲染数据
     * @param stats 统计信息（用于窗口标题）
     */
    void render(
        Camera& camera,
        const std::array<BatchData, 3>& batches,
        const std::string& stats = "");

    /**
     * 等待设备空闲
     */
    void waitIdle() const { vkDeviceWaitIdle(m_device); }

    /**
     * 处理窗口大小变化
     */
    void onResize();

private:
    // ---- Vulkan 对象 ----
    GLFWwindow*  m_window;
    VkInstance   m_instance;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    // 物理设备与逻辑设备
    VkPhysicalDevice   m_physicalDevice = VK_NULL_HANDLE;
    VkDevice           m_device = VK_NULL_HANDLE;

    // 队列
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue  = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_presentQueueFamily  = UINT32_MAX;

    // 交换链
    VkSwapchainKHR             m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage>       m_swapchainImages;
    std::vector<VkImageView>   m_swapchainImageViews;
    VkFormat                   m_swapchainImageFormat;
    VkExtent2D                 m_swapchainExtent{};
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // 渲染流程与管线
    VkRenderPass     m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;

    // 命令池与命令缓冲区
    VkCommandPool                m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // 同步对象
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence>     m_inFlightFences;
    size_t m_currentFrame = 0;

    // 深度缓冲
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;

    // 顶点/索引缓冲区（批处理）
    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
    size_t         m_currentVertexCapacity = 0;
    size_t         m_currentIndexCapacity = 0;

    // 统一缓冲区（每帧）
    std::vector<VkBuffer>       m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*>          m_uniformBuffersMapped;

    // 描述符
    VkDescriptorSetLayout           m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool                m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet>    m_descriptorSets;

    // 窗口尺寸
    int m_width = 800, m_height = 600;

    // ---- 性能优化（Pipeline 缓存） ----
    PipelineManager m_pipelineManager; // 使用 init(device) 初始化

    // ---- 性能优化（GPU 剔除 + Indirect Draw） ----
    static constexpr size_t MAX_INDIRECT_COMMANDS = 4096;
    VkDescriptorSetLayout m_cullDSLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_cullDS = VK_NULL_HANDLE;
    VkPipelineLayout      m_cullPipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_cullPipeline = VK_NULL_HANDLE;
    VkBuffer       m_cullAABBBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cullAABBMemory = VK_NULL_HANDLE;
    VkBuffer       m_cullVisibleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cullVisibleMemory = VK_NULL_HANDLE;
    VkBuffer       m_cullUBO = VK_NULL_HANDLE;
    VkDeviceMemory m_cullUBOMemory = VK_NULL_HANDLE;

    // ---- 性能优化（Indirect Draw） ----
    VkBuffer       m_indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indirectMemory = VK_NULL_HANDLE;

    // ---- 初始化方法 ----

    /** 创建 Vulkan 表面 */
    void createSurface();

    /** 选择物理设备 */
    void pickPhysicalDevice();

    /** 创建逻辑设备 */
    void createLogicalDevice();

    /** 创建交换链 */
    void createSwapchain();

    /** 创建交换链图像视图 */
    void createImageViews();

    /** 创建渲染流程 */
    void createRenderPass();

    /** 创建描述符集布局 */
    void createDescriptorSetLayout();

    /** 创建图形管线 */
    void createGraphicsPipeline();

    /** 创建深度缓冲 */
    void createDepthResources();

    /** 创建帧缓冲 */
    void createFramebuffers();

    /** 创建命令池 */
    void createCommandPool();

    /** 创建统一缓冲区 */
    void createUniformBuffers();

    /** 创建描述符池和描述符集 */
    void createDescriptorPool();

    /** 创建顶点/索引缓冲区 */
    void createBuffers(size_t vertexSize, size_t indexSize);

    /** 创建命令缓冲区 */
    void createCommandBuffers();

    /** 创建同步对象 */
    void createSyncObjects();

    // ---- 辅助方法 ----

    /** 查找内存类型 */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /** 创建缓冲区 */
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    /** 复制缓冲区 */
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    /** 创建图像视图 */
    VkImageView createImageView(VkImage image, VkFormat format,
                                 VkImageAspectFlags aspectFlags);

    /** 创建图像 */
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);

    /** 获取支持的格式 */
    VkFormat findDepthFormat();

    /** 获取支持的格式（通用） */
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features);

    /** 查询交换链详细信息 */
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    /** 选择交换链设置 */
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availableModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    /** 检查设备扩展支持 */
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    /** 评分物理设备 */
    int rateDeviceSuitability(VkPhysicalDevice device);

    /** 查找队列族 */
    struct QueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily  = UINT32_MAX;
        bool isComplete() const {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    /** 清理交换链 */
    void cleanupSwapchain();

    /** 重建交换链 */
    void recreateSwapchain();

    /** 更新统一缓冲区 */
    void updateUniformBuffer(uint32_t currentImage, Camera& camera);

    /** 录制命令缓冲区 */
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                              const std::array<BatchData, 3>& batches);

    // ---- 性能优化方法 ----

    /** 创建 GPU 剔除 Compute Pipeline */
    void createCullingPipeline();

    /** 调 GPU 视锥体剔除 */
    void dispatchFrustumCull(VkCommandBuffer cmd);

    /** 更新 Indirect 命令缓冲区 */
    void updateIndirectBuffer(const std::array<BatchData, 3>& batches);

    /** 清理 GPU 剔除资源 */
    void cleanupCullingResources();
};

// ============================================================================
// VoxelVertex Vulkan 顶点输入描述（内联函数）
// ============================================================================
inline VkVertexInputBindingDescription VoxelVertex_getBindingDescription()
{
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(VoxelVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

inline std::array<VkVertexInputAttributeDescription, 4> VoxelVertex_getAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 4> attributes{};

    // position (vec3)
    attributes[0].binding  = 0;
    attributes[0].location = 0;
    attributes[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset   = offsetof(VoxelVertex, position);

    // normal (vec3)
    attributes[1].binding  = 0;
    attributes[1].location = 1;
    attributes[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset   = offsetof(VoxelVertex, normal);

    // color (vec4)
    attributes[2].binding  = 0;
    attributes[2].location = 2;
    attributes[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset   = offsetof(VoxelVertex, color);

    // ao (float)
    attributes[3].binding  = 0;
    attributes[3].location = 3;
    attributes[3].format   = VK_FORMAT_R32_SFLOAT;
    attributes[3].offset   = offsetof(VoxelVertex, ao);

    return attributes;
}
