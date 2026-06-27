/**
 * octree.h —— 八叉树三维空间管理
 *
 * 【八叉树的作用】
 * 用于组织所有已加载的区块，支持高效的：
 * 1. 视锥体相交测试 —— 快速剔除不可见区块
 * 2. 射线检测 —— 用于玩家与方块的交互（如点击破坏）
 * 3. LOD 选择 —— 根据节点到玩家的距离决定 LOD 级别
 * 4. 空间邻近查询 —— 查找玩家周围的区块
 *
 * 【设计思路】
 * 整个世界空间被递归分割为 8 个八分体，每个节点存储该区域内的
 * 区块指针列表。叶节点包含实际的区块引用，内部节点只包含包围盒
 * 和子节点指针。
 *
 * 树的最大深度根据世界大小动态确定。每个节点覆盖一个立方体区域，
 * 其边长总是 2 的幂。
 */
#pragma once

#include "chunk.h"
#include "camera.h"

#include <memory>
#include <vector>
#include <array>
#include <functional>
#include <mutex>

#include <glm/glm.hpp>

// ============================================================================
// OctreeNode —— 八叉树节点
// ============================================================================
class OctreeNode
{
public:
    /**
     * @param min 节点区域最小值（世界坐标）
     * @param max 节点区域最大值（世界坐标）
     * @param depth 当前深度（根节点 = 0）
     * @param maxDepth 最大深度
     */
    OctreeNode(const glm::vec3& min, const glm::vec3& max,
               int depth = 0, int maxDepth = 8);

    ~OctreeNode() = default;

    /** 插入一个区块到八叉树 */
    void insert(const std::shared_ptr<Chunk>& chunk);

    /** 移除一个区块 */
    bool remove(const glm::ivec3& chunkPos);

    /**
     * 查询与视锥体相交的所有区块
     * @param planes 视锥体 6 个平面
     * @param result 输出：相交的区块列表
     */
    void queryFrustum(const std::array<Camera::FrustumPlane, 6>& planes,
                      std::vector<std::shared_ptr<Chunk>>& result) const;

    /**
     * 查询与球体相交的所有区块
     * @param center 球心
     * @param radius 半径
     * @param result 输出：相交的区块列表
     */
    void querySphere(const glm::vec3& center, float radius,
                     std::vector<std::shared_ptr<Chunk>>& result) const;

    /**
     * 获取指定位置附近的区块，按距离排序
     * @param position 参考位置
     * @param radius 搜索半径（区块单位）
     * @param result 输出：区块列表
     */
    void queryNearby(const glm::vec3& position, int radius,
                     std::vector<std::shared_ptr<Chunk>>& result) const;

    /** 清空所有子节点和区块 */
    void clear();

    /** 获取区域内的区块总数 */
    size_t chunkCount() const { return m_chunks.size() + childChunkCount(); }

    /** 调试：打印树结构 */
    void debugPrint(int indent = 0) const;

private:
    glm::vec3 m_min, m_max;     // 节点区域（世界坐标）
    glm::vec3 m_center;         // 区域中心
    int m_depth;                // 当前深度
    int m_maxDepth;             // 最大深度

    // 当前节点存储的区块（如果未进一步细分）
    std::vector<std::shared_ptr<Chunk>> m_chunks;

    // 子节点（8 个八分体）
    std::array<std::unique_ptr<OctreeNode>, 8> m_children;

    /** 判断节点包围盒是否与视锥体相交 */
    bool intersectsFrustum(const std::array<Camera::FrustumPlane, 6>& planes) const;

    /** 判断节点包围盒是否与球体相交 */
    bool intersectsSphere(const glm::vec3& center, float radius) const;

    /** 获取子节点中的区块总数 */
    size_t childChunkCount() const;

    /** 划分节点：创建 8 个子节点 */
    void subdivide();
};

// ============================================================================
// Octree —— 八叉树管理类
// ============================================================================
class Octree
{
public:
    /**
     * @param worldSize 世界大小（半径，以区块为单位）
     * @param chunksPerAxis 每个轴向上的区块数
     */
    Octree(int worldSize = 512, int maxDepth = 8);

    /** 插入区块 */
    void insert(const std::shared_ptr<Chunk>& chunk)
    {
        m_root->insert(chunk);
    }

    /** 移除区块 */
    bool remove(const glm::ivec3& chunkPos)
    {
        return m_root->remove(chunkPos);
    }

    /** 视锥体查询 */
    void queryFrustum(const std::array<Camera::FrustumPlane, 6>& planes,
                      std::vector<std::shared_ptr<Chunk>>& result) const
    {
        m_root->queryFrustum(planes, result);
    }

    /** 球体查询 */
    void querySphere(const glm::vec3& center, float radius,
                     std::vector<std::shared_ptr<Chunk>>& result) const
    {
        m_root->querySphere(center, radius, result);
    }

    /** 邻近查询 */
    void queryNearby(const glm::vec3& position, int radius,
                     std::vector<std::shared_ptr<Chunk>>& result) const
    {
        m_root->queryNearby(position, radius, result);
    }

    /** 清空 */
    void clear() { m_root->clear(); }

    /** 获取区块总数 */
    size_t chunkCount() const { return m_root ? m_root->chunkCount() : 0; }

private:
    std::unique_ptr<OctreeNode> m_root;
};
