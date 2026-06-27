/**
 * render_graph.h —— 轻量级 Render Graph 自动化资源管理
 *
 * 【优化目标】
 * 1. 自动管理 Pass 间的 Barrier（资源状态转换）
 * 2. Transient 资源复用（临时颜色/深度缓冲）
 * 3. 按依赖 DAG 自动调度 Pass 执行顺序
 * 4. 透明 Pass 执行，简化主渲染循环
 *
 * 【使用示例】
 *   RenderGraph rg(device);
 *   rg.addResource("MainColor", ColorAttachment, format, extent);
 *   rg.addPass({"MainScene", {}, {"MainColor", "MainDepth"}, renderFunc});
 *   rg.addPass({"UI", {"MainColor"}, {"SwapchainImage"}, uiFunc});
 *   rg.compile();
 *   rg.execute(cmd, frameIndex);
 */
#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <cstdint>

// ============================================================================
// ResourceType —— 资源类型枚举
// ============================================================================
enum class RGResourceType {
    ColorAttachment,
    DepthAttachment,
    InputAttachment,
    StorageImage,
    Buffer
};

// ============================================================================
// RGResource —— Render Graph 资源描述
// ============================================================================
struct RGResource {
    std::string name;
    RGResourceType type = RGResourceType::ColorAttachment;
    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent{};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    bool transient = true;               // 临时资源，可复用
    bool external = false;               // 外部资源（如 Swapchain），不管理生命周期

    // Vulkan 对象
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool inUse = false;

    // 生命周期
    int firstPass = -1;   // 第一次使用的 Pass 索引
    int lastPass  = -1;   // 最后一次使用的 Pass 索引
};

// ============================================================================
// RGPass —— Render Graph Pass 定义
// ============================================================================
struct RGPass {
    std::string name;
    std::vector<std::string> inputs;   // 输入资源
    std::vector<std::string> outputs;  // 输出资源
    std::function<void(VkCommandBuffer)> execute; // 渲染函数
    bool usesDepth = false;

    // 可选：自定义 clear 颜色
    VkClearValue clearColor = {{{0.5f, 0.7f, 1.0f, 1.0f}}};
    bool useCustomClear = false;
};

// ============================================================================
// RenderGraph —— 核心类
// ============================================================================
class RenderGraph
{
public:
    RenderGraph(VkDevice device, VkPhysicalDevice physicalDevice)
        : m_device(device)
        , m_physicalDevice(physicalDevice)
    {
    }

    ~RenderGraph()
    {
        cleanup();
    }

    // 禁止拷贝
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    /**
     * 添加资源声明
     */
    RGResource& addResource(const std::string& name,
                             RGResourceType type,
                             VkFormat format,
                             VkExtent2D extent,
                             bool transient = true,
                             bool external = false)
    {
        auto [it, ok] = m_resources.try_emplace(name);

        if (!ok)
            it->second = RGResource(); // 已存在则覆盖

        it->second.name = name;
        it->second.type = type;
        it->second.format = format;
        it->second.extent = extent;
        it->second.transient = transient;
        it->second.external = external;

        // 根据类型设置默认 usage
        switch (type)
        {
        case RGResourceType::ColorAttachment:
            it->second.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            break;
        case RGResourceType::DepthAttachment:
            it->second.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            break;
        default:
            break;
        }

        return it->second;
    }

    /**
     * 添加 Pass
     */
    void addPass(RGPass pass)
    {
        m_passes.push_back(std::move(pass));
    }

    /**
     * 编译：分析依赖、分配资源、创建 VkRenderPass
     */
    void compile(VkDevice device)
    {
        m_device = device;

        // 1. 计算资源使用区间
        for (int p = 0; p < static_cast<int>(m_passes.size()); ++p)
        {
            auto& pass = m_passes[p];

            auto markUsage = [&](const std::string& name, int passIdx) {
                auto it = m_resources.find(name);
                if (it == m_resources.end()) return;
                if (it->second.firstPass < 0) it->second.firstPass = passIdx;
                it->second.lastPass = passIdx;
            };

            for (auto& input : pass.inputs)   markUsage(input, p);
            for (auto& output : pass.outputs)  markUsage(output, p);
        }

        // 2. 为 transient 资源创建实际 Vulkan 对象
        for (auto& [name, res] : m_resources)
        {
            if (res.transient && !res.external)
            {
                createImage(res);
            }
        }

        // 3. 为每个 Pass 创建 RenderPass 和 Framebuffer
        for (size_t p = 0; p < m_passes.size(); ++p)
        {
            createPassResources(static_cast<int>(p));
        }

        m_compiled = true;
    }

    /**
     * 执行所有 Pass
     */
    void execute(VkCommandBuffer cmd, uint32_t frameIndex)
    {
        if (!m_compiled) return;

        for (size_t p = 0; p < m_passes.size(); ++p)
        {
            auto& pass = m_passes[p];

            // Barrier: 输入资源转换到正确 layout
            for (auto& inputName : pass.inputs)
            {
                auto it = m_resources.find(inputName);
                if (it == m_resources.end()) continue;
                auto& res = it->second;

                VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (res.type == RGResourceType::DepthAttachment)
                    targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                transitionLayout(cmd, res.image,
                    res.currentLayout, targetLayout,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                res.currentLayout = targetLayout;
            }

            // Barrier: 输出资源转换到 attachment layout
            for (auto& outputName : pass.outputs)
            {
                auto it = m_resources.find(outputName);
                if (it == m_resources.end()) continue;
                auto& res = it->second;

                VkImageLayout targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                VkAccessFlags access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

                if (res.type == RGResourceType::DepthAttachment)
                {
                    targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                }

                transitionLayout(cmd, res.image,
                    res.currentLayout, targetLayout,
                    access, stage);
                res.currentLayout = targetLayout;
            }

            // 执行 Pass
            pass.execute(cmd);
        }
    }

    /**
     * 清理资源
     */
    void cleanup()
    {
        for (auto& [name, res] : m_resources)
        {
            if (res.transient && !res.external)
            {
                if (res.view != VK_NULL_HANDLE)
                    vkDestroyImageView(m_device, res.view, nullptr);
                if (res.image != VK_NULL_HANDLE)
                    vkDestroyImage(m_device, res.image, nullptr);
                if (res.memory != VK_NULL_HANDLE)
                    vkFreeMemory(m_device, res.memory, nullptr);
            }
        }
        m_resources.clear();
        m_passes.clear();

        for (auto& fb : m_framebuffers)
            vkDestroyFramebuffer(m_device, fb, nullptr);
        for (auto& rp : m_renderPasses)
            vkDestroyRenderPass(m_device, rp, nullptr);

        m_framebuffers.clear();
        m_renderPasses.clear();
        m_compiled = false;
    }

    /** 获取资源 */
    RGResource* getResource(const std::string& name)
    {
        auto it = m_resources.find(name);
        return it != m_resources.end() ? &it->second : nullptr;
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    bool m_compiled = false;

    std::unordered_map<std::string, RGResource> m_resources;
    std::vector<RGPass> m_passes;
    std::vector<VkFramebuffer> m_framebuffers;
    std::vector<VkRenderPass> m_renderPasses;

    // ---- 辅助方法 ----

    void createImage(RGResource& res)
    {
        VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format    = res.format;
        ci.extent    = {res.extent.width, res.extent.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples   = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling    = VK_IMAGE_TILING_OPTIMAL;
        ci.usage     = res.usage;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult result = vkCreateImage(m_device, &ci, nullptr, &res.image);
        if (result != VK_SUCCESS) return;

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_device, res.image, &memReq);

        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = memReq.size;
        ai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result = vkAllocateMemory(m_device, &ai, nullptr, &res.memory);
        if (result != VK_SUCCESS) return;

        vkBindImageMemory(m_device, res.image, res.memory, 0);

        res.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // 创建 ImageView
        VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image    = res.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = res.format;
        vci.subresourceRange.aspectMask = (res.type == RGResourceType::DepthAttachment)
            ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;

        vkCreateImageView(m_device, &vci, nullptr, &res.view);
    }

    void createPassResources(int passIndex)
    {
        auto& pass = m_passes[passIndex];
        // 为每个 Pass 创建 RenderPass + Framebuffer 的复杂逻辑
        // ... 实际实现中需根据 inputs/outputs 构建
    }

    void transitionLayout(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkAccessFlags dstAccess,
                          VkPipelineStageFlags dstStage)
    {
        if (oldLayout == newLayout) return;

        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image            = image;
        barrier.oldLayout        = oldLayout;
        barrier.newLayout        = newLayout;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        // 自动推导 srcAccess 和 srcStage
        VkAccessFlags srcAccess = 0;
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            srcAccess = 0;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            srcAccess = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            srcAccess = 0;
            srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }

        VkImageAspectFlags aspect = (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                                     newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.aspectMask = aspect;

        vkCmdPipelineBarrier(cmd, srcStage, dstStage,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        return 0;
    }
};
