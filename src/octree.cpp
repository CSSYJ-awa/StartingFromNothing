/**
 * octree.cpp —— 八叉树实现
 */
#include "octree.h"
#include "camera.h" // 需要 FrustumPlane

#include <iostream>
#include <algorithm>

// ============================================================================
// OctreeNode 实现
// ============================================================================

OctreeNode::OctreeNode(const glm::vec3& min, const glm::vec3& max,
                       int depth, int maxDepth)
    : m_min(min)
    , m_max(max)
    , m_center((min + max) * 0.5f)
    , m_depth(depth)
    , m_maxDepth(maxDepth)
{
}

void OctreeNode::insert(const std::shared_ptr<Chunk>& chunk)
{
    auto [chunkMin, chunkMax] = chunk->getBoundingBox();

    // 检查区块是否在该节点区域内
    if (chunkMax.x < m_min.x || chunkMin.x > m_max.x ||
        chunkMax.y < m_min.y || chunkMin.y > m_max.y ||
        chunkMax.z < m_min.z || chunkMin.z > m_max.z)
    {
        return; // 不在该节点区域内
    }

    // 如果已细分，尝试插入到子节点
    if (m_children[0])
    {
        for (auto& child : m_children)
        {
            if (child)
                child->insert(chunk);
        }
        return;
    }

    // 未细分：直接存储
    m_chunks.push_back(chunk);

    // 如果达到分裂阈值且未达到最大深度，则细分
    if (m_chunks.size() > 4 && m_depth < m_maxDepth)
    {
        subdivide();
        // 将当前节点的区块重新分配到子节点
        auto chunks = std::move(m_chunks);
        for (auto& c : chunks)
        {
            for (auto& child : m_children)
                child->insert(c);
        }
    }
}

bool OctreeNode::remove(const glm::ivec3& chunkPos)
{
    // 从当前节点移除
    auto it = std::remove_if(m_chunks.begin(), m_chunks.end(),
        [&](const std::shared_ptr<Chunk>& c) {
            return c->m_position == chunkPos;
        });
    bool removed = (it != m_chunks.end());
    m_chunks.erase(it, m_chunks.end());

    // 递归从子节点移除
    if (m_children[0])
    {
        for (auto& child : m_children)
        {
            if (child)
                removed |= child->remove(chunkPos);
        }
    }

    return removed;
}

void OctreeNode::queryFrustum(
    const std::array<Camera::FrustumPlane, 6>& planes,
    std::vector<std::shared_ptr<Chunk>>& result) const
{
    // 先做节点级视锥体测试
    if (!intersectsFrustum(planes))
        return; // 节点完全在视锥体外 → 该子树全部剔除

    // 将当前节点的区块加入结果
    for (const auto& chunk : m_chunks)
    {
        // 对每个区块做精确的 AABB-视锥体测试
        auto [min, max] = chunk->getBoundingBox();
        bool inside = true;
        for (const auto& plane : planes)
        {
            // 使用 AABB 的"正顶点"测试
            glm::vec3 positiveVertex = min;
            if (plane.normal.x >= 0) positiveVertex.x = max.x;
            if (plane.normal.y >= 0) positiveVertex.y = max.y;
            if (plane.normal.z >= 0) positiveVertex.z = max.z;

            if (glm::dot(plane.normal, positiveVertex) < plane.distance)
            {
                inside = false;
                break;
            }
        }
        if (inside)
            result.push_back(chunk);
    }

    // 递归处理子节点
    if (m_children[0])
    {
        for (const auto& child : m_children)
        {
            if (child)
                child->queryFrustum(planes, result);
        }
    }
}

void OctreeNode::querySphere(
    const glm::vec3& center, float radius,
    std::vector<std::shared_ptr<Chunk>>& result) const
{
    if (!intersectsSphere(center, radius))
        return;

    // 检查当前节点的区块
    for (const auto& chunk : m_chunks)
    {
        auto [min, max] = chunk->getBoundingBox();
        glm::vec3 chunkCenter = (min + max) * 0.5f;
        float dist = glm::length(chunkCenter - center);
        if (dist <= radius + CHUNK_SIZE * 1.732f * 0.5f) // 对角线半长
        {
            result.push_back(chunk);
        }
    }

    // 递归子节点
    if (m_children[0])
    {
        for (const auto& child : m_children)
        {
            if (child)
                child->querySphere(center, radius, result);
        }
    }
}

void OctreeNode::queryNearby(
    const glm::vec3& position, int radius,
    std::vector<std::shared_ptr<Chunk>>& result) const
{
    // 计算搜索范围
    float searchRadius = static_cast<float>(radius * CHUNK_SIZE);

    // 如果节点区域与搜索范围不相交，跳过
    glm::vec3 nearest(
        glm::clamp(position.x, m_min.x, m_max.x),
        glm::clamp(position.y, m_min.y, m_max.y),
        glm::clamp(position.z, m_min.z, m_max.z)
    );
    float distSq = glm::dot(nearest - position, nearest - position);
    if (distSq > searchRadius * searchRadius)
        return;

    // 检查当前节点的区块
    for (const auto& chunk : m_chunks)
    {
        auto [min, max] = chunk->getBoundingBox();
        glm::vec3 chunkCenter = (min + max) * 0.5f;
        float d = glm::length(chunkCenter - position);
        if (d <= searchRadius + CHUNK_SIZE * 1.732f)
        {
            result.push_back(chunk);
        }
    }

    // 递归子节点
    if (m_children[0])
    {
        for (const auto& child : m_children)
        {
            if (child)
                child->queryNearby(position, radius, result);
        }
    }
}

void OctreeNode::clear()
{
    m_chunks.clear();
    for (auto& child : m_children)
    {
        if (child)
        {
            child->clear();
            child.reset();
        }
    }
}

size_t OctreeNode::childChunkCount() const
{
    size_t count = 0;
    if (m_children[0])
    {
        for (const auto& child : m_children)
        {
            if (child)
                count += child->chunkCount();
        }
    }
    return count;
}

void OctreeNode::subdivide()
{
    if (m_children[0])
        return; // 已经细分

    glm::vec3 half = (m_max - m_min) * 0.5f;
    int childDepth = m_depth + 1;

    // 创建 8 个子节点
    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 childMin(
            (i & 1) ? m_center.x : m_min.x,
            (i & 2) ? m_center.y : m_min.y,
            (i & 4) ? m_center.z : m_min.z
        );
        glm::vec3 childMax = childMin + half;

        m_children[i] = std::make_unique<OctreeNode>(
            childMin, childMax, childDepth, m_maxDepth);
    }
}

bool OctreeNode::intersectsFrustum(
    const std::array<Camera::FrustumPlane, 6>& planes) const
{
    for (const auto& plane : planes)
    {
        // 使用 AABB 的"正顶点"测试
        glm::vec3 positiveVertex = m_min;
        if (plane.normal.x >= 0) positiveVertex.x = m_max.x;
        if (plane.normal.y >= 0) positiveVertex.y = m_max.y;
        if (plane.normal.z >= 0) positiveVertex.z = m_max.z;

        if (glm::dot(plane.normal, positiveVertex) < plane.distance)
            return false;
    }
    return true;
}

bool OctreeNode::intersectsSphere(const glm::vec3& center, float radius) const
{
    glm::vec3 nearest(
        glm::clamp(center.x, m_min.x, m_max.x),
        glm::clamp(center.y, m_min.y, m_max.y),
        glm::clamp(center.z, m_min.z, m_max.z)
    );
    float distSq = glm::dot(nearest - center, nearest - center);
    return distSq <= radius * radius;
}

void OctreeNode::debugPrint(int indent) const
{
    std::string prefix(indent, ' ');
    std::cout << prefix << "Node [" << m_depth << "] "
              << "min(" << m_min.x << "," << m_min.y << "," << m_min.z << ") "
              << "max(" << m_max.x << "," << m_max.y << "," << m_max.z << ") "
              << "chunks=" << m_chunks.size()
              << " total=" << chunkCount()
              << std::endl;

    if (m_children[0])
    {
        for (const auto& child : m_children)
        {
            if (child && child->chunkCount() > 0)
                child->debugPrint(indent + 2);
        }
    }
}

// ============================================================================
// Octree 实现
// ============================================================================

Octree::Octree(int worldSize, int maxDepth)
{
    float halfWorld = static_cast<float>(worldSize * CHUNK_SIZE);
    glm::vec3 min(-halfWorld, -halfWorld, -halfWorld);
    glm::vec3 max(halfWorld, halfWorld, halfWorld);
    m_root = std::make_unique<OctreeNode>(min, max, 0, maxDepth);
}
