/**
 * optimization_manager.h —— 性能优化管理器
 *
 * 统筹管理所有性能优化策略，包括：
 *
 * 1. 【背面剔除】
 *    已在 MeshBuilder 中实现——仅生成暴露在外的表面。
 *
 * 2. 【视锥体剔除】
 *    在 CPU 端使用 Camera::getFrustumPlanes() 获取视锥体 6 个平面，
 *    然后通过 Octree::queryFrustum() 快速筛选可见区块。
 *
 * 3. 【远景体素 LOD 渲染】
 *    根据玩家到区块中心的距离选择 LOD 级别：
 *    - 近距离 (0-80 方块): LOD0（全分辨率）
 *    - 中距离 (80-200 方块): LOD1（2×2×2 合并）
 *    - 远距离 (200-500 方块): LOD2（4×4×4 合并）
 *    - 超过 500 方块: 不渲染
 *
 * 4. 【远近分区差异化渲染】
 *    近、中、远三个区域分别使用不同 LOD 和着色器复杂度：
 *    - 近区 (0-80): 全光照 + 全细节
 *    - 中区 (80-200): 简化光照 + LOD1
 *    - 远区 (200-500): 无光照 + LOD2（仅颜色）
 *
 * 5. 【几何体合批】
 *    将同一 LOD 级别的所有可见区块的顶点数据合并到单个
 *    Vulkan 顶点缓冲区中，使用单个绘制调用提交。
 *    按 LOD 级别分组，最多 3 个 draw call。
 *
 * 6. 【异步后台生成 + 缓存调度】
 *    地形生成和网格构建在 ThreadPool 中异步执行。
 *    已完成的任务结果在主线程的 completePendingWork() 中收集。
 *
 * 【渲染分区定义】
 *  近区 (Near):  距离 ≤ ZONE_NEAR    → LOD0
 *  中区 (Medium): ZONE_NEAR < 距离 ≤ ZONE_MEDIUM → LOD1
 *  远区 (Far):    ZONE_MEDIUM < 距离 ≤ ZONE_FAR  → LOD2
 */
#pragma once

#include "chunk.h"
#include "camera.h"

#include <vector>
#include <memory>
#include <glm/glm.hpp>

// ============================================================================
// 分区距离常量（世界坐标方块数）
// ============================================================================
constexpr float ZONE_NEAR   = 80.0f;   // 近区范围
constexpr float ZONE_MEDIUM = 200.0f;  // 中区范围
constexpr float ZONE_FAR    = 500.0f;  // 远区范围

// ============================================================================
// 合批数据 —— 每个 LOD 级别的批处理缓冲区
// ============================================================================
struct BatchData
{
    std::vector<VoxelVertex> vertices;  // 合并后的顶点数据
    std::vector<uint32_t>    indices;   // 合并后的索引数据
    size_t                   chunkCount = 0; // 参与合并的区块数
    LODLevel                 lod = LODLevel::LOD0_HIGH;

    void clear()
    {
        vertices.clear();
        indices.clear();
        chunkCount = 0;
    }
};

// ============================================================================
// OptimizationManager —— 优化管理器
// ============================================================================
class OptimizationManager
{
public:
    OptimizationManager() = default;

    /**
     * 对所有可见区块执行优化流程（距离版 LOD）
     */
    void processChunks(
        const std::vector<std::shared_ptr<Chunk>>& visibleChunks,
        const glm::vec3& playerPos,
        std::array<BatchData, 3>& batches);

    /**
     * 屏幕空间动态 LOD 版本
     * @param viewProj 当前 VP 矩阵
     * @param screenW/H 屏幕像素尺寸
     */
    void processChunksScreenSpace(
        const std::vector<std::shared_ptr<Chunk>>& visibleChunks,
        const glm::mat4& viewProj,
        int screenW, int screenH,
        std::array<BatchData, 3>& batches);

    /** 基于距离的 LOD 选择 */
    static LODLevel selectLOD(float distance);

    /** 基于屏幕像素尺寸的 LOD 选择 */
    static int selectLODByScreenSize(
        const glm::vec3& chunkCenter,
        const glm::mat4& viewProj,
        int screenWidth, int screenHeight);

    static int getZone(float distance);

    // ---- 统计信息 ----

    /** 获取近区区块数 */
    size_t getNearCount() const { return m_nearCount; }

    /** 获取中区区块数 */
    size_t getMediumCount() const { return m_mediumCount; }

    /** 获取远区区块数 */
    size_t getFarCount() const { return m_farCount; }

    /** 获取总顶点数 */
    size_t getTotalVertices() const { return m_totalVertices; }

    /** 获取总三角面数 */
    size_t getTotalTriangles() const { return m_totalTriangles; }

    /** 获取 Draw Call 数（通常 = 使用的 LOD 级别数） */
    int getDrawCallCount() const { return m_drawCallCount; }

    /** 获取剔除比例 */
    float getCullingRatio() const
    {
        if (m_totalChunks == 0) return 0.0f;
        return 1.0f - static_cast<float>(m_visibleAfterFrustum) / static_cast<float>(m_totalChunks);
    }

private:
    // ---- 统计 ----
    size_t m_nearCount = 0;
    size_t m_mediumCount = 0;
    size_t m_farCount = 0;
    size_t m_totalVertices = 0;
    size_t m_totalTriangles = 0;
    int    m_drawCallCount = 0;
    size_t m_totalChunks = 0;
    size_t m_visibleAfterFrustum = 0;

    /**
     * 执行几何体合批
     * 将同一 LOD 级别的区块顶点数据合并到同一个 BatchData 中
     */
    void batchChunks(
        const std::vector<std::shared_ptr<Chunk>>& chunks,
        LODLevel lod,
        BatchData& batch);
};
