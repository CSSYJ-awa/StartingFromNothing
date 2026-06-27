/**
 * chunk.cpp —— 区块数据与网格构建实现
 */
#include "chunk.h"
#include "block_registry.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

// ============================================================================
// 全局区块缓存初始化
// ============================================================================
std::unordered_map<uint64_t, std::shared_ptr<Chunk>> Chunk::s_chunkCache;
std::mutex Chunk::s_cacheMutex;

// ============================================================================
// Chunk 实现
// ============================================================================

Chunk::Chunk(const glm::ivec3& pos)
    : m_position(pos)
{
    std::fill(m_blocks.begin(), m_blocks.end(), static_cast<uint16_t>(BlockID::AIR));
    std::fill(m_metadata.begin(), m_metadata.end(), static_cast<uint8_t>(0));
}

uint16_t Chunk::getBlock(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
        return static_cast<uint16_t>(BlockID::AIR);
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_blocks[index(x, y, z)];
}

void Chunk::setBlock(int x, int y, int z, uint16_t type)
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blocks[index(x, y, z)] = type;
    m_meshDirty.store(true);
}

uint8_t Chunk::getMetadata(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
        return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metadata[index(x, y, z)];
}

void Chunk::setMetadata(int x, int y, int z, uint8_t meta)
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metadata[index(x, y, z)] = meta;
}

uint16_t Chunk::getBlockAt(const glm::ivec3& worldPos)
{
    auto chunkPos = worldToChunk(glm::vec3(worldPos));
    auto chunk = getChunk(chunkPos);
    if (!chunk) return static_cast<uint16_t>(BlockID::AIR);
    auto local = worldToLocal(glm::vec3(worldPos));
    return chunk->getBlock(local.x, local.y, local.z);
}

void Chunk::setBlockAt(const glm::ivec3& worldPos, uint16_t type)
{
    auto chunkPos = worldToChunk(glm::vec3(worldPos));
    auto chunk = getOrCreateChunk(chunkPos);
    auto local = worldToLocal(glm::vec3(worldPos));
    chunk->setBlock(local.x, local.y, local.z, type);
}

std::pair<glm::vec3, glm::vec3> Chunk::getBoundingBox() const
{
    glm::vec3 min(
        m_position.x * CHUNK_SIZE,
        m_position.y * CHUNK_SIZE,
        m_position.z * CHUNK_SIZE
    );
    glm::vec3 max = min + glm::vec3(CHUNK_SIZE);
    return {min, max};
}

glm::vec3 Chunk::getCenter() const
{
    return glm::vec3(
        m_position.x * CHUNK_SIZE + CHUNK_SIZE / 2.0f,
        m_position.y * CHUNK_SIZE + CHUNK_SIZE / 2.0f,
        m_position.z * CHUNK_SIZE + CHUNK_SIZE / 2.0f
    );
}

int Chunk::getTopBlockY() const
{
    for (int y = CHUNK_SIZE - 1; y >= 0; --y)
    {
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
                if (m_blocks[index(x, y, z)] != static_cast<uint16_t>(BlockID::AIR))
                    return y;
            }
        }
    }
    return 0;
}

// ---- 全局区块缓存 ----

std::shared_ptr<Chunk> Chunk::getOrCreateChunk(const glm::ivec3& chunkPos)
{
    uint64_t key = chunkKey(chunkPos.x, chunkPos.y, chunkPos.z);
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    auto it = s_chunkCache.find(key);
    if (it != s_chunkCache.end())
        return it->second;

    auto chunk = std::make_shared<Chunk>(chunkPos);
    s_chunkCache[key] = chunk;
    return chunk;
}

std::shared_ptr<Chunk> Chunk::getChunk(const glm::ivec3& chunkPos)
{
    uint64_t key = chunkKey(chunkPos.x, chunkPos.y, chunkPos.z);
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    auto it = s_chunkCache.find(key);
    return (it != s_chunkCache.end()) ? it->second : nullptr;
}

void Chunk::removeChunk(const glm::ivec3& chunkPos)
{
    uint64_t key = chunkKey(chunkPos.x, chunkPos.y, chunkPos.z);
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_chunkCache.erase(key);
}

// ============================================================================
// MeshBuilder 实现
//
// 【表面提取算法】
// 对区块中每个非空气方块，检查其 6 个面。
// 若某个面的相邻方块为空气或透明方块，则生成该面的两个三角形。
//
// 【顶点布局】
// 每个面由 6 个顶点（两个三角形）构成，四边形顶点顺序为：
//   v0--v1
//   |  / |
//   v2--v3
//
// 【LOD 合并】
// LOD1: 将 2×2×2 区域视为一个"宏方块"，
//       若该区域内至少有一个非空气方块则生成宏方块的暴露面。
// LOD2: 将 4×4×4 区域视为一个宏方块。
// ============================================================================

void MeshBuilder::buildMesh(const Chunk& chunk, LODLevel lod, LODMeshData& output)
{
    output.vertices.clear();
    output.indices.clear();
    output.valid = false;

    int merge = LOD_MERGE_SIZES[static_cast<int>(lod)];
    const auto& registry = BlockRegistry::instance();

    // 遍历区块中的所有"宏方块"
    for (int my = 0; my < CHUNK_SIZE; my += merge)
    {
        for (int mz = 0; mz < CHUNK_SIZE; mz += merge)
        {
            for (int mx = 0; mx < CHUNK_SIZE; mx += merge)
            {
                // 检查该宏方块区域是否有非空气方块
                bool hasBlock = false;
                glm::vec4 avgColor(0.0f);
                int solidCount = 0;
                uint16_t dominantType = static_cast<uint16_t>(BlockID::AIR);

                for (int dy = 0; dy < merge; ++dy)
                {
                    for (int dz = 0; dz < merge; ++dz)
                    {
                        for (int dx = 0; dx < merge; ++dx)
                        {
                            int bx = mx + dx;
                            int by = my + dy;
                            int bz = mz + dz;
                            if (bx >= CHUNK_SIZE || by >= CHUNK_SIZE || bz >= CHUNK_SIZE)
                                continue;

                            uint16_t type = chunk.data()[Chunk::index(bx, by, bz)];
                            if (type != static_cast<uint16_t>(BlockID::AIR) &&
                                !registry.getBlock(type).isTransparent)
                            {
                                hasBlock = true;
                                avgColor += registry.getBlock(type).color;
                                solidCount++;
                                dominantType = type;
                            }
                        }
                    }
                }

                if (!hasBlock)
                    continue;

                // 计算平均颜色
                if (solidCount > 0)
                    avgColor /= static_cast<float>(solidCount);

                // 检查 6 个面是否需要生成
                constexpr BlockFace faces[] = {
                    BlockFace::TOP, BlockFace::BOTTOM,
                    BlockFace::FRONT, BlockFace::BACK,
                    BlockFace::LEFT, BlockFace::RIGHT
                };

                // 宏方块位置（世界坐标）
                glm::vec3 worldPos(
                    chunk.m_position.x * CHUNK_SIZE + mx,
                    chunk.m_position.y * CHUNK_SIZE + my,
                    chunk.m_position.z * CHUNK_SIZE + mz
                );

                for (auto face : faces)
                {
                    if (shouldGenerateFace(chunk, mx, my, mz, face, lod, merge))
                    {
                        addFace(output, worldPos, face, avgColor,
                                static_cast<float>(merge));
                    }
                }
            }
        }
    }

    output.valid = !output.vertices.empty();
}

/**
 * 检查某个宏方块的指定面是否需要生成网格。
 *
 * 【背面剔除原理】
 * 对面 direction 方向的相邻位置进行检查。如果相邻位置：
 * 1. 在区块内部 → 检查相邻方块是否为空气或透明
 * 2. 在区块外部 → 检查相邻区块的对应方块
 *
 * 只有当相邻处为空气或透明方块时，才生成该面（因为该面可见）。
 * 两个固体方块相邻的面永远不可见，因此不生成——这就是"背面剔除"。
 */
bool MeshBuilder::shouldGenerateFace(
    const Chunk& chunk,
    int mx, int my, int mz,
    BlockFace face,
    LODLevel lod, int merge)
{
    int dx = 0, dy = 0, dz = 0;

    switch (face)
    {
    case BlockFace::TOP:    dy = merge; break;
    case BlockFace::BOTTOM: dy = -merge; break;
    case BlockFace::FRONT:  dz = merge; break;
    case BlockFace::BACK:   dz = -merge; break;
    case BlockFace::RIGHT:  dx = merge; break;
    case BlockFace::LEFT:   dx = -merge; break;
    default: break;
    }

    // 相邻区域在区块内的范围
    int nx = mx + dx;
    int ny = my + dy;
    int nz = mz + dz;

    // 检查相邻区域是否在区块内
    if (nx >= 0 && nx < CHUNK_SIZE &&
        ny >= 0 && ny < CHUNK_SIZE &&
        nz >= 0 && nz < CHUNK_SIZE)
    {
        // 内部相邻：检查对应的宏方块区域
        // 只需检查相邻位置对应宏方块内的一个代表性方块
        // 对于同尺寸 LOD 合并，相邻宏方块的首个方块即可代表
        // 这里采用保守策略：检查相邻面上是否有至少一个非空气非透明方块
        int checkCount = 0;
        int transparentCount = 0;

        for (int dy2 = 0; dy2 < merge && ny + dy2 < CHUNK_SIZE; ++dy2)
        {
            for (int dz2 = 0; dz2 < merge && nz + dz2 < CHUNK_SIZE; ++dz2)
            {
                for (int dx2 = 0; dx2 < merge && nx + dx2 < CHUNK_SIZE; ++dx2)
                {
                    int cx = nx + dx2;
                    int cy = ny + dy2;
                    int cz = nz + dz2;
                    if (cx >= CHUNK_SIZE || cy >= CHUNK_SIZE || cz >= CHUNK_SIZE)
                        continue;

                    uint16_t type = chunk.data()[Chunk::index(cx, cy, cz)];
                    if (type == static_cast<uint16_t>(BlockID::AIR))
                        transparentCount++;
                    else if (BlockRegistry::instance().isTransparent(type))
                        transparentCount++;
                    checkCount++;
                }
            }
        }

        // 如果所有相邻方块都是固体，则该面不可见
        return (transparentCount > 0);
    }
    else
    {
        // 相邻区域在区块外部：需要检查相邻区块
        glm::ivec3 neighborChunkPos = chunk.m_position;
        glm::ivec3 neighborLocal(nx, ny, nz);

        // 处理越界
        if (nx < 0) { neighborChunkPos.x--; neighborLocal.x += CHUNK_SIZE; }
        if (nx >= CHUNK_SIZE) { neighborChunkPos.x++; neighborLocal.x -= CHUNK_SIZE; }
        if (ny < 0) { neighborChunkPos.y--; neighborLocal.y += CHUNK_SIZE; }
        if (ny >= CHUNK_SIZE) { neighborChunkPos.y++; neighborLocal.y -= CHUNK_SIZE; }
        if (nz < 0) { neighborChunkPos.z--; neighborLocal.z += CHUNK_SIZE; }
        if (nz >= CHUNK_SIZE) { neighborChunkPos.z++; neighborLocal.z -= CHUNK_SIZE; }

        // 检查相邻区块中对应位置的方块
        auto neighborChunk = Chunk::getChunk(neighborChunkPos);
        if (!neighborChunk)
            return true; // 相邻区块未加载 → 保守生成该面

        // 取相邻位置对应宏方块的代表方块
        int remapX = neighborLocal.x;
        int remapY = neighborLocal.y;
        int remapZ = neighborLocal.z;

        // 对于 LOD 合并，检查相邻面范围内的所有方块
        int transparentCount = 0;
        int totalCheck = 0;
        for (int dy2 = 0; dy2 < merge; ++dy2)
        {
            for (int dz2 = 0; dz2 < merge; ++dz2)
            {
                for (int dx2 = 0; dx2 < merge; ++dx2)
                {
                    int cx = remapX + dx2;
                    int cy = remapY + dy2;
                    int cz = remapZ + dz2;
                    if (cx < 0 || cx >= CHUNK_SIZE ||
                        cy < 0 || cy >= CHUNK_SIZE ||
                        cz < 0 || cz >= CHUNK_SIZE)
                        continue;

                    uint16_t type = neighborChunk->getBlock(cx, cy, cz);
                    if (type == static_cast<uint16_t>(BlockID::AIR))
                        transparentCount++;
                    else if (BlockRegistry::instance().isTransparent(type))
                        transparentCount++;
                    totalCheck++;
                }
            }
        }

        return (transparentCount > 0 || totalCheck == 0);
    }
}

/**
 * 向网格中添加一个面（2 个三角形，6 个顶点）
 *
 *         v0───v1
 *         │ ╱  │
 *         v2───v3
 *
 * 每个面垂直于一个坐标轴，由 4 个顶点构成一个四边形。
 * 法线方向根据面的朝向确定。
 */
void MeshBuilder::addFace(
    LODMeshData& output,
    const glm::vec3& pos,
    BlockFace face,
    const glm::vec4& color,
    float blockSize)
{
    // 面的 4 个顶点位置偏移（相对于 pos，单位长度）
    // 注：这里假设 Y 轴向上，坐标系为右手系
    struct QuadVerts { glm::vec3 v0, v1, v2, v3; };
    QuadVerts quad;

    // 法线方向
    glm::vec3 normal(0.0f);

    switch (face)
    {
    case BlockFace::TOP:
        normal = glm::vec3(0, 1, 0);
        // CCW 从外部（从上方看）：左前 → 右前 → 左后 → 右后
        quad.v0 = glm::vec3(0, blockSize, blockSize);     // 左前
        quad.v1 = glm::vec3(blockSize, blockSize, blockSize); // 右前
        quad.v2 = glm::vec3(0, blockSize, 0);             // 左后
        quad.v3 = glm::vec3(blockSize, blockSize, 0);     // 右后
        break;
    case BlockFace::BOTTOM:
        normal = glm::vec3(0, -1, 0);
        // CCW 从外部（从下方看）：左后 → 右后 → 左前 → 右前
        quad.v0 = glm::vec3(0, 0, 0);                     // 左后
        quad.v1 = glm::vec3(blockSize, 0, 0);             // 右后
        quad.v2 = glm::vec3(0, 0, blockSize);             // 左前
        quad.v3 = glm::vec3(blockSize, 0, blockSize);     // 右前
        break;
    case BlockFace::FRONT:
        normal = glm::vec3(0, 0, 1);
        quad.v0 = glm::vec3(0, 0, blockSize);
        quad.v1 = glm::vec3(blockSize, 0, blockSize);
        quad.v2 = glm::vec3(0, blockSize, blockSize);
        quad.v3 = glm::vec3(blockSize, blockSize, blockSize);
        break;
    case BlockFace::BACK:
        normal = glm::vec3(0, 0, -1);
        quad.v0 = glm::vec3(blockSize, 0, 0);
        quad.v1 = glm::vec3(0, 0, 0);
        quad.v2 = glm::vec3(blockSize, blockSize, 0);
        quad.v3 = glm::vec3(0, blockSize, 0);
        break;
    case BlockFace::LEFT:
        normal = glm::vec3(-1, 0, 0);
        quad.v0 = glm::vec3(0, 0, 0);
        quad.v1 = glm::vec3(0, 0, blockSize);
        quad.v2 = glm::vec3(0, blockSize, 0);
        quad.v3 = glm::vec3(0, blockSize, blockSize);
        break;
    case BlockFace::RIGHT:
        normal = glm::vec3(1, 0, 0);
        quad.v0 = glm::vec3(blockSize, 0, blockSize);
        quad.v1 = glm::vec3(blockSize, 0, 0);
        quad.v2 = glm::vec3(blockSize, blockSize, blockSize);
        quad.v3 = glm::vec3(blockSize, blockSize, 0);
        break;
    }

    // 将局部偏移转换为世界坐标
    quad.v0 += pos;
    quad.v1 += pos;
    quad.v2 += pos;
    quad.v3 += pos;

    // 计算环境光遮蔽因子
    // 简化的 AO：根据面方向给不同亮度（模拟漫反射光照）
    float ao = 0.7f + 0.3f * normal.y; // 顶面亮，底面暗
    if (face == BlockFace::BOTTOM) ao = 0.5f;

    // 添加 6 个顶点（两个三角形）
    VoxelVertex verts[4];
    for (int i = 0; i < 4; ++i)
    {
        verts[i] = {
            i == 0 ? quad.v0 : (i == 1 ? quad.v1 : (i == 2 ? quad.v2 : quad.v3)),
            normal,
            color,
            ao
        };
    }

    // 三角形 1: v0-v1-v2
    output.vertices.push_back(verts[0]);
    output.vertices.push_back(verts[1]);
    output.vertices.push_back(verts[2]);

    // 三角形 2: v1-v3-v2
    output.vertices.push_back(verts[1]);
    output.vertices.push_back(verts[3]);
    output.vertices.push_back(verts[2]);
}

void MeshBuilder::buildAllLODs(const Chunk& chunk, LODMeshData meshes[3])
{
    for (int i = 0; i < 3; ++i)
    {
        LODLevel lod = static_cast<LODLevel>(i);
        buildMesh(chunk, lod, meshes[i]);
    }
}
