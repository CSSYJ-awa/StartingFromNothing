/**
 * perf_integration.h —— 性能优化集成头文件
 *
 * 将 pipeline_manager.h 和 memory_pool.h 集成到现有工程。
 * 本文件包含辅助类型和集成声明。
 */
#pragma once

#include "pipeline_manager.h"
#include "memory_pool.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>
#include <cstdint>

// ============================================================================
// Indirect Draw 相关数据结构
// ============================================================================

/** 每区块的 GPU 槽位信息（全局缓冲区的偏移） */
struct ChunkGPUSlot {
    uint32_t vertexOffset = 0;  // 全局顶点缓冲区偏移（顶点数）
    uint32_t indexOffset  = 0;  // 全局索引缓冲区偏移（索引数）
    uint32_t indexCount[3] = {0, 0, 0}; // 各 LOD 索引数
    bool     valid = false;
};

/** 每 LOD 的 Indirect Batch */
struct IndirectBatch {
    std::vector<VkDrawIndexedIndirectCommand> commands;
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
    bool dirty = true;

    void addCommand(uint32_t indexCount, uint32_t indexOffset)
    {
        commands.push_back({indexCount, 1, indexOffset, 0, 0});
        dirty = true;
    }

    void clear()
    {
        commands.clear();
        dirty = true;
    }
};

// ============================================================================
// GPU 剔除相关数据结构
// ============================================================================

/** 视锥体参数（与 Compute Shader 共享布局） */
struct GPUFrustumData {
    struct Plane { glm::vec4 equation; }; // xyz=normal, w=distance
    Plane planes[6];
    glm::mat4 viewProj;
    glm::ivec2 screenSize;
    float _pad[2];
};

// ============================================================================
// RenderGraph 相关（轻量版）
// ============================================================================

enum class RGResourceType {
    ColorAttachment,
    DepthAttachment,
    Buffer,
    Image
};

struct RGResourceDesc {
    std::string name;
    RGResourceType type;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    bool transient = true;
};

// ============================================================================
// 纹理图集相关
// ============================================================================

/** 方块 UV 偏移表 */
struct BlockUV {
    float u_offset, v_offset;
    float u_scale, v_scale;
};

// ============================================================================
// ECS 组件（使用 entt 时的参考定义）
// ============================================================================
// 若引入 entt 库，可按以下方式定义组件：
//
// #include <entt/entt.hpp>
//
// struct Position { glm::vec3 value; };
// struct Velocity { glm::vec3 value; };
// struct PlayerTag {};
// struct ChunkData { glm::ivec3 pos; ChunkState state; };
// struct MeshLOD { std::array<LODMeshData, 3> lods; };
