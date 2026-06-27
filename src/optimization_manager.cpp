/**
 * optimization_manager.cpp —— 性能优化管理器实现
 */
#include "optimization_manager.h"

#include <algorithm>
#include <iostream>

LODLevel OptimizationManager::selectLOD(float distance)
{
    if (distance <= ZONE_NEAR)
        return LODLevel::LOD0_HIGH;
    else if (distance <= ZONE_MEDIUM)
        return LODLevel::LOD1_MEDIUM;
    else
        return LODLevel::LOD2_LOW;
}

int OptimizationManager::getZone(float distance)
{
    if (distance <= ZONE_NEAR) return 0;
    if (distance <= ZONE_MEDIUM) return 1;
    return 2;
}

/**
 * 屏幕空间 LOD 选择
 * 将区块的 8 个角点投影到屏幕，根据包围盒的屏幕像素尺寸选择 LOD。
 * 大尺寸（近距离）用高分辨率 LOD，小尺寸（远距离）用低分辨率 LOD。
 */
int OptimizationManager::selectLODByScreenSize(
    const glm::vec3& chunkCenter,
    const glm::mat4& viewProj,
    int screenWidth, int screenHeight)
{
    const float halfSize = CHUNK_SIZE * 0.5f;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;

    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 corner = chunkCenter + glm::vec3(
            (i & 1 ? 1 : -1) * halfSize,
            (i & 2 ? 1 : -1) * halfSize,
            (i & 4 ? 1 : -1) * halfSize);

        glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
        glm::vec2 ndc = glm::vec2(clip) / clip.w;

        minX = std::min(minX, ndc.x); maxX = std::max(maxX, ndc.x);
        minY = std::min(minY, ndc.y); maxY = std::max(maxY, ndc.y);
    }

    float pixelW = (maxX - minX) * screenWidth  * 0.5f;
    float pixelH = (maxY - minY) * screenHeight * 0.5f;
    float screenSize = std::max(pixelW, pixelH);

    if (screenSize > 80.0f) return 0;  // LOD0 全分辨率
    if (screenSize > 30.0f) return 1;  // LOD1 2×2×2
    return 2;                          // LOD2 4×4×4 或更少
}

/**
 * 屏幕空间动态 LOD 版 processChunks
 * 使用投影后的像素尺寸选择 LOD，比基于距离的选择更精确。
 */
void OptimizationManager::processChunksScreenSpace(
    const std::vector<std::shared_ptr<Chunk>>& visibleChunks,
    const glm::mat4& viewProj,
    int screenW, int screenH,
    std::array<BatchData, 3>& batches)
{
    m_nearCount = m_mediumCount = m_farCount = 0;
    m_totalVertices = m_totalTriangles = 0;
    m_drawCallCount = 0;
    for (auto& b : batches) b.clear();

    m_totalChunks = visibleChunks.size();
    m_visibleAfterFrustum = 0;

    std::vector<std::shared_ptr<Chunk>> lodChunks[3];

    for (const auto& chunk : visibleChunks)
    {
        if (!chunk || !chunk->isReady()) continue;
        m_visibleAfterFrustum++;

        int lod = selectLODByScreenSize(
            chunk->getCenter(), viewProj, screenW, screenH);
        while (lod > 0 && !chunk->getMeshData(static_cast<LODLevel>(lod)).valid)
            lod--;

        int zone = (lod == 0) ? 0 : (lod == 1 ? 1 : 2);
        switch (zone) {
            case 0: m_nearCount++; break;
            case 1: m_mediumCount++; break;
            case 2: m_farCount++; break;
        }

        const auto& mesh = chunk->getMeshData(static_cast<LODLevel>(lod));
        if (mesh.valid) lodChunks[lod].push_back(chunk);
    }

    for (int i = 0; i < 3; ++i)
    {
        if (!lodChunks[i].empty())
        {
            batchChunks(lodChunks[i], static_cast<LODLevel>(i), batches[i]);
            m_drawCallCount++;
        }
    }
}

void OptimizationManager::processChunks(
    const std::vector<std::shared_ptr<Chunk>>& visibleChunks,
    const glm::vec3& playerPos,
    std::array<BatchData, 3>& batches)
{
    // 重置统计
    m_nearCount = m_mediumCount = m_farCount = 0;
    m_totalVertices = m_totalTriangles = 0;
    m_drawCallCount = 0;

    // 清空批次数据
    for (auto& batch : batches)
        batch.clear();

    m_totalChunks = visibleChunks.size();
    m_visibleAfterFrustum = 0;

    // 按 LOD 级别分组收集
    std::vector<std::shared_ptr<Chunk>> lodChunks[3];

    for (const auto& chunk : visibleChunks)
    {
        if (!chunk || !chunk->isReady())
            continue;

        m_visibleAfterFrustum++;

        float distance = glm::length(chunk->getCenter() - playerPos);
        LODLevel lod = selectLOD(distance);
        int zone = getZone(distance);

        switch (zone)
        {
        case 0: m_nearCount++; break;
        case 1: m_mediumCount++; break;
        case 2: m_farCount++; break;
        }

        // 检查该 LOD 级别的网格是否有效
        if (chunk->getMeshData(lod).valid)
        {
            lodChunks[static_cast<int>(lod)].push_back(chunk);
        }
        else
        {
            // 回退到较低 LOD
            for (int fallback = static_cast<int>(lod) - 1; fallback >= 0; --fallback)
            {
                LODLevel fallbackLod = static_cast<LODLevel>(fallback);
                if (chunk->getMeshData(fallbackLod).valid)
                {
                    lodChunks[fallback].push_back(chunk);
                    break;
                }
            }
        }
    }

    // 执行合批
    for (int i = 0; i < 3; ++i)
    {
        LODLevel lod = static_cast<LODLevel>(i);
        if (!lodChunks[i].empty())
        {
            batchChunks(lodChunks[i], lod, batches[i]);
            m_drawCallCount++;
        }
    }
}

void OptimizationManager::batchChunks(
    const std::vector<std::shared_ptr<Chunk>>& chunks,
    LODLevel lod,
    BatchData& batch)
{
    batch.lod = lod;
    batch.chunkCount = chunks.size();

    size_t totalVerts = 0;
    size_t totalIndices = 0;

    // 计算总需要的顶点/索引数
    for (const auto& chunk : chunks)
    {
        const auto& mesh = chunk->getMeshData(lod);
        if (mesh.valid)
        {
            totalVerts += mesh.vertices.size();
            totalIndices += mesh.indices.size();
        }
    }

    // 预留空间
    batch.vertices.reserve(totalVerts);
    batch.indices.reserve(totalIndices);

    // 合并顶点和索引数据
    uint32_t vertexOffset = 0;
    for (const auto& chunk : chunks)
    {
        const auto& mesh = chunk->getMeshData(lod);
        if (!mesh.valid)
            continue;

        // 复制顶点
        batch.vertices.insert(batch.vertices.end(),
                              mesh.vertices.begin(), mesh.vertices.end());

        // 复制索引（偏移后）
        for (uint32_t idx : mesh.indices)
        {
            batch.indices.push_back(idx + vertexOffset);
        }

        vertexOffset += static_cast<uint32_t>(mesh.vertices.size());
    }

    // 更新统计
    m_totalVertices += batch.vertices.size();
    m_totalTriangles += batch.indices.size() / 3;
}
