/**
 * pipeline_manager.h —— Pipeline 状态缓存与重用管理器
 *
 * 【优化目标】
 * 1. 使用 VkPipelineCache 缓存编译后的着色器状态，跨启动持久化
 * 2. 按 (Shader 组合, 顶点布局, RenderPass) 的 hash 缓存 Pipeline，
 *    避免重复创建相同的 PSO
 *
 * 【预期收益】
 * - 二次启动 Pipeline 创建时间从 ~100ms 降至 ~5ms
 * - 多次请求相同 PSO 时自动复用，避免 GPU 驱动重复编译
 */
#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <cstdint>
#include <functional>

// ============================================================================
// PipelineManager —— Pipeline 缓存管理器
// ============================================================================
class PipelineManager
{
public:
    PipelineManager() = default;

    explicit PipelineManager(VkDevice device)
        : m_device(device)
    {
    }

    ~PipelineManager()
    {
        if (!m_cleaned) cleanup();
    }

    /** 后初始化（用于默认构造后设置 device） */
    void init(VkDevice device, const std::string& cachePath = "vulkan_pipeline_cache.bin")
    {
        m_device = device;
        m_cachePath = cachePath;
        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        std::ifstream file(m_cachePath, std::ios::binary | std::ios::ate);
        if (file.is_open())
        {
            size_t size = static_cast<size_t>(file.tellg());
            m_cachedData.resize(size);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(m_cachedData.data()), size);
            ci.initialDataSize = size;
            ci.pInitialData    = m_cachedData.data();
        }

        VkResult result = vkCreatePipelineCache(m_device, &ci, nullptr, &m_cache);
        if (result != VK_SUCCESS)
        {
            ci.initialDataSize = 0;
            ci.pInitialData = nullptr;
            vkCreatePipelineCache(m_device, &ci, nullptr, &m_cache);
        }
    }

    // 禁止拷贝
    PipelineManager(const PipelineManager&) = delete;
    PipelineManager& operator=(const PipelineManager&) = delete;

    /** 清理资源 */
    void cleanup()
    {
        if (m_cleaned) return;
        m_cleaned = true;
        saveToDisk();
        for (auto& [_, pipe] : m_pipelines)
        {
            if (pipe != VK_NULL_HANDLE)
                vkDestroyPipeline(m_device, pipe, nullptr);
        }
        m_pipelines.clear();
        if (m_cache != VK_NULL_HANDLE)
        {
            vkDestroyPipelineCache(m_device, m_cache, nullptr);
            m_cache = VK_NULL_HANDLE;
        }
    }

    /**
     * 保存 PipelineCache 到磁盘
     */
    void saveToDisk()
    {
        if (m_cache == VK_NULL_HANDLE) return;

        size_t dataSize;
        vkGetPipelineCacheData(m_device, m_cache, &dataSize, nullptr);
        if (dataSize == 0) return;

        std::vector<uint8_t> data(dataSize);
        vkGetPipelineCacheData(m_device, m_cache, &dataSize, data.data());

        std::ofstream file(m_cachePath, std::ios::binary);
        if (file.is_open())
        {
            file.write(reinterpret_cast<char*>(data.data()), dataSize);
        }
    }

    /**
     * 创建或获取缓存的 Pipeline
     * @param ci Pipeline 创建信息
     * @return VkPipeline
     */
    VkPipeline getOrCreatePipeline(const VkGraphicsPipelineCreateInfo& ci)
    {
        size_t key = hashPipeline(ci);
        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end())
            return it->second;

        VkPipeline pipeline;
        VkResult result = vkCreateGraphicsPipelines(
            m_device, m_cache, 1, &ci, nullptr, &pipeline);

        if (result != VK_SUCCESS)
            return VK_NULL_HANDLE;

        m_pipelines[key] = pipeline;
        return pipeline;
    }

    /**
     * 创建或获取缓存的 Compute Pipeline
     */
    VkPipeline getOrCreateComputePipeline(const VkComputePipelineCreateInfo& ci)
    {
        size_t key = 0;
        hashCombine(key, reinterpret_cast<size_t>(ci.stage.module));
        hashCombine(key, static_cast<size_t>(ci.stage.stage));

        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end())
            return it->second;

        VkPipeline pipeline;
        VkResult result = vkCreateComputePipelines(
            m_device, m_cache, 1, &ci, nullptr, &pipeline);

        if (result != VK_SUCCESS)
            return VK_NULL_HANDLE;

        m_pipelines[key] = pipeline;
        return pipeline;
    }

    VkPipelineCache getCache() const { return m_cache; }

private:
    VkDevice m_device;
    VkPipelineCache m_cache = VK_NULL_HANDLE;
    std::string m_cachePath;
    std::vector<uint8_t> m_cachedData;
    std::unordered_map<size_t, VkPipeline> m_pipelines;
    bool m_cleaned = false;

    /** 计算 Pipeline 创建信息的 hash 值 */
    static size_t hashPipeline(const VkGraphicsPipelineCreateInfo& ci)
    {
        size_t h = 0;

        // 着色器 stage hash
        for (uint32_t i = 0; i < ci.stageCount; ++i)
        {
            hashCombine(h, reinterpret_cast<size_t>(ci.pStages[i].module));
            hashCombine(h, static_cast<size_t>(ci.pStages[i].stage));
        }

        // 顶点输入布局 hash
        if (ci.pVertexInputState)
        {
            hashCombine(h, ci.pVertexInputState->vertexBindingDescriptionCount);
            hashCombine(h, ci.pVertexInputState->vertexAttributeDescriptionCount);
        }

        // 渲染流程 hash
        hashCombine(h, reinterpret_cast<size_t>(ci.renderPass));

        // 子流程索引
        hashCombine(h, static_cast<size_t>(ci.subpass));

        return h;
    }

    static void hashCombine(size_t& seed, size_t value)
    {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    static size_t hashCombineValue(size_t seed, size_t value)
    {
        hashCombine(seed, value);
        return seed;
    }
};
