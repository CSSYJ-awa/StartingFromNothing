/**
 * chunk.h —— 区块数据模块
 *
 * 区块是体素世界的基本数据单元，固定大小为 16×16×16 个方块。
 *
 * 【紧凑存储格式】
 * 每个方块使用 uint16_t（类型 ID）+ uint8_t（元数据）= 3 字节
 * 一个区块总计 16×16×16 = 4096 个方块 → 约 12 KB 原始数据
 *
 * 【LOD 支持】
 * 区块可以生成多个 LOD 级别的网格：
 *   LOD0: 全分辨率 (16×16×16)
 *   LOD1: 2×2×2 合并 (8×8×8)
 *   LOD2: 4×4×4 合并 (4×4×4)
 */
#pragma once

#include "block_registry.h"

#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cmath>

#include <glm/glm.hpp>

// ============================================================================
// 常量定义
// ============================================================================
constexpr int CHUNK_SIZE     = 16;  // 区块边长（方块数）
constexpr int CHUNK_VOLUME   = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
constexpr int CHUNK_HEIGHT   = 256; // 世界高度（竖直方向总方块数）

// ============================================================================
// LOD 级别
// ============================================================================
enum class LODLevel : uint8_t
{
    LOD0_HIGH   = 0, // 全分辨率
    LOD1_MEDIUM = 1, // 2×2×2 合并
    LOD2_LOW    = 2, // 4×4×4 合并
    COUNT       = 3
};

/** LOD 对应的合并尺寸 */
constexpr int LOD_MERGE_SIZES[] = { 1, 2, 4 };

// ============================================================================
// ChunkState —— 区块状态标志
// ============================================================================
enum class ChunkState : uint8_t
{
    UNLOADED    = 0, // 未加载
    GENERATING  = 1, // 地形生成中
    GENERATED   = 2, // 地形已生成
    MESHING     = 3, // 网格构建中
    MESHED      = 4, // 网格已就绪
    READY       = 5  // 可渲染
};

// ============================================================================
// 顶点结构（用于 Vulkan 渲染）
// ============================================================================
struct VoxelVertex
{
    glm::vec3 position; // 位置 (float)
    glm::vec3 normal;   // 法线 (float)
    glm::vec4 color;    // 颜色 (float) — 替代纹理
    float     ao;       // 环境光遮蔽因子 [0, 1]
};

/** LOD 级别的顶点数据结构 */
struct LODMeshData
{
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t>    indices;
    bool                     valid = false;
};



// ============================================================================
// Chunk —— 区块数据
// ============================================================================
class Chunk
{
public:
    /** 区块的世界坐标（以区块为单位） */
    glm::ivec3 m_position;

    Chunk() = default;
    explicit Chunk(const glm::ivec3& pos);

    // ---- 方块访问 ----

    /** 获取方块类型 ID（线程安全） */
    uint16_t getBlock(int x, int y, int z) const;

    /** 设置方块类型 ID（线程安全） */
    void setBlock(int x, int y, int z, uint16_t type);

    /** 获取方块元数据 */
    uint8_t getMetadata(int x, int y, int z) const;

    /** 设置方块元数据 */
    void setMetadata(int x, int y, int z, uint8_t meta);

    /** 获取全局坐标下的方块 */
    static uint16_t getBlockAt(const glm::ivec3& worldPos);

    /** 设置全局坐标下的方块 */
    static void setBlockAt(const glm::ivec3& worldPos, uint16_t type);

    // ---- 状态管理 ----

    ChunkState getState() const { return m_state.load(); }
    void setState(ChunkState state) { m_state.store(state); }

    bool isReady() const { return m_state.load() == ChunkState::READY; }

    // ---- LOD 网格管理 ----

    LODMeshData& getMeshData(LODLevel lod) { return m_meshData[static_cast<int>(lod)]; }
    const LODMeshData& getMeshData(LODLevel lod) const { return m_meshData[static_cast<int>(lod)]; }

    void setMeshDirty(bool dirty = true) { m_meshDirty.store(dirty); }
    bool isMeshDirty() const { return m_meshDirty.load(); }

    /** 标记所有 LOD 网格为无效 */
    void invalidateMeshes()
    {
        for (auto& mesh : m_meshData)
            mesh.valid = false;
    }

    /** 获取区块包围盒（世界坐标） */
    std::pair<glm::vec3, glm::vec3> getBoundingBox() const;

    /** 获取区块中心点（世界坐标） */
    glm::vec3 getCenter() const;

    // ---- 快速访问 ----

    /** 获取原始方块数据指针（用于批量操作） */
    uint16_t* data() { return m_blocks.data(); }
    const uint16_t* data() const { return m_blocks.data(); }

    /** 获取原始元数据指针（用于批量操作） */
    uint8_t* metaData() { return m_metadata.data(); }
    const uint8_t* metaData() const { return m_metadata.data(); }

    /** 获取区块高度（最高非空气方块 Y 值） */
    int getTopBlockY() const;

    /** 三维坐标转一维索引 */
    static int index(int x, int y, int z)
    {
        return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
    }

public:
    /** 将 3D 世界坐标编码为 64 位 key */
    static uint64_t chunkKey(int cx, int cy, int cz)
    {
        // 使用 21 位每分量（支持约 ±100 万区块范围）
        return (static_cast<uint64_t>(static_cast<int32_t>(cx)) & 0x1FFFFF) |
               ((static_cast<uint64_t>(static_cast<int32_t>(cy)) & 0x1FFFFF) << 21) |
               ((static_cast<uint64_t>(static_cast<int32_t>(cz)) & 0x1FFFFF) << 42);
    }

    /** 从 64 位 key 解码区块坐标 */
    static glm::ivec3 chunkKeyDecode(uint64_t key)
    {
        int cx = static_cast<int>(key & 0x1FFFFF);
        if (cx & 0x100000) cx |= ~0x1FFFFF;
        int cy = static_cast<int>((key >> 21) & 0x1FFFFF);
        if (cy & 0x100000) cy |= ~0x1FFFFF;
        int cz = static_cast<int>((key >> 42) & 0x1FFFFF);
        if (cz & 0x100000) cz |= ~0x1FFFFF;
        return glm::ivec3(cx, cy, cz);
    }

private:
    std::array<uint16_t, CHUNK_VOLUME> m_blocks;
    std::array<uint8_t, CHUNK_VOLUME>  m_metadata;
    std::atomic<ChunkState>            m_state{ChunkState::UNLOADED};
    std::atomic<bool>                  m_meshDirty{true};
    std::array<LODMeshData, 3>         m_meshData; // 3 个 LOD 级别
    mutable std::mutex                 m_mutex;

    // 全局区块缓存（世界坐标 → 区块）
    static std::unordered_map<uint64_t, std::shared_ptr<Chunk>> s_chunkCache;
    static std::mutex s_cacheMutex;

public:
    // ---- 全局区块缓存访问 ----

    /** 获取或创建指定坐标的区块 */
    static std::shared_ptr<Chunk> getOrCreateChunk(const glm::ivec3& chunkPos);

    /** 获取指定坐标的区块（不存在则返回 nullptr） */
    static std::shared_ptr<Chunk> getChunk(const glm::ivec3& chunkPos);

    /** 移除指定坐标的区块 */
    static void removeChunk(const glm::ivec3& chunkPos);

    /** 世界坐标 → 区块坐标 */
    static glm::ivec3 worldToChunk(const glm::vec3& worldPos)
    {
        return glm::ivec3(
            static_cast<int>(std::floor(worldPos.x / CHUNK_SIZE)),
            static_cast<int>(std::floor(worldPos.y / CHUNK_SIZE)),
            static_cast<int>(std::floor(worldPos.z / CHUNK_SIZE))
        );
    }

    /** 世界坐标 → 区块内局部坐标 */
    static glm::ivec3 worldToLocal(const glm::vec3& worldPos)
    {
        return glm::ivec3(
            static_cast<int>(std::floor(worldPos.x)) & (CHUNK_SIZE - 1),
            static_cast<int>(std::floor(worldPos.y)) & (CHUNK_SIZE - 1),
            static_cast<int>(std::floor(worldPos.z)) & (CHUNK_SIZE - 1)
        );
    }
};

// ============================================================================
// MeshBuilder —— 区块网格构建器
//
// 【核心算法：表面提取 + 背面剔除】
// 对每个区块遍历所有方块，仅当某个面与空气或透明方块相邻时才
// 生成该面的三角形。这大幅减少了顶点数量。
//
// 【LOD 合并策略】
// LOD1: 将 2×2×2 的 8 个方块合并为 1 个大的"宏方块"
// LOD2: 将 4×4×4 的 64 个方块合并
// 合并后的宏方块取多数决确定类型，颜色为各合并方块的平均值
// ============================================================================
class MeshBuilder
{
public:
    /**
     * 构建某个 LOD 级别的网格
     * @param chunk  源区块
     * @param lod    LOD 级别
     * @param output 输出网格数据
     */
    static void buildMesh(const Chunk& chunk, LODLevel lod, LODMeshData& output);

    /**
     * 为所有 LOD 级别构建网格
     */
    static void buildAllLODs(const Chunk& chunk,
                             LODMeshData meshes[3]);

private:
    /**
     * 检查某个面是否需要生成（相邻为空气或透明方块则生成）
     * @param chunk  区块数据
     * @param x,y,z  方块坐标（区块局部）
     * @param face   面方向
     * @param lod    LOD 级别
     * @param merge  LOD 合并尺寸
     */
    static bool shouldGenerateFace(
        const Chunk& chunk,
        int x, int y, int z,
        BlockFace face,
        LODLevel lod, int merge);

    /**
     * 生成一个面的顶点数据
     */
    static void addFace(
        LODMeshData& output,
        const glm::vec3& pos,
        BlockFace face,
        const glm::vec4& color,
        float blockSize);
};
