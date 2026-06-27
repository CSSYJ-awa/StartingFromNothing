/**
 * world.h —— 世界管理器
 *
 * 管理体素世界的整体生命周期：
 * 1. 区块的动态加载/卸载（根据玩家位置）
 * 2. 地形生成的任务调度（异步）
 * 3. 网格构建的任务调度（异步）
 * 4. 已加载区块缓存
 * 5. 空间查询（视锥体、邻近等）
 *
 * 【异步调度策略】
 * 1. 每帧根据玩家位置计算需要加载的区块列表
 * 2. 将地形生成和网格构建任务提交到线程池
 * 3. 离玩家近的区块使用 HIGH 优先级
 * 4. 远离玩家的区块使用 LOW 优先级
 * 5. 超出加载范围的区块被卸载
 *
 * 【加载范围】
 * - 水平加载半径：8 个区块（128 个方块）
 * - 垂直加载半径：4 个区块（64 个方块）
 * - LOD 渲染半径：32 个区块（512 个方块）
 */
#pragma once

#include "chunk.h"
#include "octree.h"
#include "terrain_generator.h"
#include "thread_pool.h"
#include "camera.h"

#include <memory>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <fstream>
#include <glm/glm.hpp>

// ============================================================================
// World —— 世界管理器
// ============================================================================
class World
{
public:
    /**
     * @param seed 世界种子
     * @param horizontalLoadRadius 水平加载半径（区块数）
     * @param verticalLoadRadius 垂直加载半径（区块数）
     */
    World(uint32_t seed = 42,
          int horizontalLoadRadius = 10,
          int verticalLoadRadius = 6);

    ~World();

    // 禁止拷贝
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /**
     * 每帧更新：根据玩家位置加载/卸载区块
     * @param playerPos 玩家世界坐标
     */
    void update(const glm::vec3& playerPos);

    /**
     * 完成异步任务的结果收集
     * 在主线程调用，将后台完成的区块数据整合到渲染管线
     */
    void completePendingWork();

    /**
     * 同步生成玩家周围初始区块
     */
    void generateInitialChunks(const glm::vec3& centerPos, int radius = 3);

    // ---- 异步加载增强 ----

    /** 保存区块到磁盘缓存 */
    static void saveChunkToDisk(const glm::ivec3& pos, const uint16_t* blocks, const uint8_t* meta);

    /** 从磁盘缓存加载区块，返回 true 表示成功 */
    static bool loadChunkFromDisk(const glm::ivec3& pos, uint16_t* blocks, uint8_t* meta);

    /** 获取世界文件夹路径 */
    static std::string worldCachePath();

    /**
     * 获取所有可见区块（经过视锥体剔除后）
     * @param frustumPlanes 视锥体 6 个平面
     * @param result 输出可见区块列表
     * @param playerPos 玩家位置（用于 LOD 选择）
     */
    void getVisibleChunks(
        const std::array<Camera::FrustumPlane, 6>& frustumPlanes,
        std::vector<std::shared_ptr<Chunk>>& result,
        const glm::vec3& playerPos);

    /** 获取八叉树引用 */
    Octree& getOctree() { return m_octree; }

    /** 获取地形生成器 */
    TerrainGenerator& getTerrainGenerator() { return m_terrainGen; }

    /** 获取线程池 */
    ThreadPool& getThreadPool() { return m_threadPool; }

    /** 获取当前已加载区块总数 */
    size_t getLoadedChunkCount() const { return m_loadedChunks.load(); }

    /** 获取待处理的生成任务数 */
    int getPendingGenerateCount() const { return m_pendingGenerate; }

    /** 获取待处理的网格任务数 */
    int getPendingMeshCount() const { return m_pendingMesh; }

    /** 获取当前待处理任务总数 */
    int getTotalPendingTasks() const
    {
        return static_cast<int>(m_threadPool.pendingTasks());
    }

private:
    // ---- 核心组件 ----
    TerrainGenerator m_terrainGen;
    Octree           m_octree;

    // ---- 线程安全队列（必须在 ThreadPool 之前声明，确保最后销毁）----
    std::vector<std::shared_ptr<Chunk>> m_pendingOctreeInserts;
    mutable std::mutex m_pendingInsertsMutex;

    ThreadPool       m_threadPool;

    // ---- 配置参数 ----
    int m_horizontalLoadRadius;
    int m_verticalLoadRadius;

    // ---- 状态 ----
    std::unordered_set<uint64_t> m_loadedChunkKeys; // 已加载的区块 key 集合
    std::atomic<size_t> m_loadedChunks{0};
    std::atomic<int> m_pendingGenerate{0};
    std::atomic<int> m_pendingMesh{0};

    // 上次更新时的玩家区块坐标
    glm::ivec3 m_lastPlayerChunk{999999, 999999, 999999};

    // ---- 内部方法 ----

    /**
     * 生成指定坐标的区块（异步调用）
     */
    void generateChunkAsync(const glm::ivec3& chunkPos, ThreadPool::Priority priority);

    /**
     * 构建区块网格（异步调用）
     */
    void meshChunkAsync(const std::shared_ptr<Chunk>& chunk, ThreadPool::Priority priority);

    /**
     * 卸载指定坐标的区块
     */
    void unloadChunk(const glm::ivec3& chunkPos);

    /**
     * 根据玩家位置确定区块的优先级
     */
    ThreadPool::Priority getPriority(const glm::ivec3& chunkPos,
                                      const glm::ivec3& playerChunk) const;

    /**
     * 将后台完成的区块插入八叉树（主线程调用，线程安全）
     */
    void flushPendingInserts();
};
