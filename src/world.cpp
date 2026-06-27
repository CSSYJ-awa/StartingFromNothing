/**
 * world.cpp —— 世界管理器实现
 */
#include "world.h"
#include "block_registry.h"
#include "camera.h"

#include <algorithm>
#include <iostream>
#include <glm/glm.hpp>

World::~World()
{
    // ThreadPool 析构时会等待所有后台任务完成，
    // 其他成员都在 ThreadPool 之后析构，因此访问安全。
}

World::World(uint32_t seed,
             int horizontalLoadRadius,
             int verticalLoadRadius)
    : m_terrainGen(seed)
    , m_octree(1024, 8)
    , m_threadPool()
    , m_horizontalLoadRadius(horizontalLoadRadius)
    , m_verticalLoadRadius(verticalLoadRadius)
{
    std::cout << "[World] 初始化完成。种子: " << seed
              << ", 加载半径: " << horizontalLoadRadius
              << " x " << verticalLoadRadius
              << ", 工作线程: " << m_threadPool.workerCount() << std::endl;
}

void World::update(const glm::vec3& playerPos)
{
    glm::ivec3 playerChunk = Chunk::worldToChunk(playerPos);

    // 每帧都将后台完成的区块插入八叉树（即使玩家未移动）
    flushPendingInserts();

    // 只在玩家移动到新区块时触发加载/卸载
    if (playerChunk == m_lastPlayerChunk)
        return;

    m_lastPlayerChunk = playerChunk;

    // 计算需要加载的区块范围
    int minX = playerChunk.x - m_horizontalLoadRadius;
    int maxX = playerChunk.x + m_horizontalLoadRadius;
    int minZ = playerChunk.z - m_horizontalLoadRadius;
    int maxZ = playerChunk.z + m_horizontalLoadRadius;
    int minY = playerChunk.y - m_verticalLoadRadius;
    int maxY = playerChunk.y + m_verticalLoadRadius;

    // 收集需要加载的区块
    std::vector<glm::ivec3> toLoad;
    std::unordered_set<uint64_t> neededKeys;

    for (int x = minX; x <= maxX; ++x)
    {
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                glm::ivec3 chunkPos(x, y, z);
                uint64_t ckey = Chunk::chunkKey(x, y, z);

                neededKeys.insert(ckey);

                if (m_loadedChunkKeys.find(ckey) == m_loadedChunkKeys.end())
                {
                    toLoad.push_back(chunkPos);
                }
            }
        }
    }

    // 提交生成任务
    for (const auto& pos : toLoad)
    {
        auto priority = getPriority(pos, playerChunk);
        generateChunkAsync(pos, priority);
    }

    // 卸载不再需要的区块
    std::vector<uint64_t> toUnload;
    for (const auto& key : m_loadedChunkKeys)
    {
        if (neededKeys.find(key) == neededKeys.end())
        {
            toUnload.push_back(key);
        }
    }

    for (const auto& key : toUnload)
    {
        m_loadedChunkKeys.erase(key);
        glm::ivec3 chunkPos = Chunk::chunkKeyDecode(key);
        m_octree.remove(chunkPos);
        Chunk::removeChunk(chunkPos);
    }

    m_loadedChunks.store(m_loadedChunkKeys.size());
}

void World::flushPendingInserts()
{
    std::vector<std::shared_ptr<Chunk>> pending;
    {
        std::lock_guard<std::mutex> lock(m_pendingInsertsMutex);
        pending.swap(m_pendingOctreeInserts);
    }
    for (auto& chunk : pending)
    {
        m_octree.insert(chunk);
    }
}

void World::completePendingWork()
{
    // 已由 flushPendingInserts() 替代处理
}

void World::getVisibleChunks(
    const std::array<Camera::FrustumPlane, 6>& frustumPlanes,
    std::vector<std::shared_ptr<Chunk>>& result,
    const glm::vec3& playerPos)
{
    // 使用八叉树进行视锥体剔除
    m_octree.queryFrustum(frustumPlanes, result);

    // 对可见区块按距离排序（近→远），便于 LOD 选择
    std::sort(result.begin(), result.end(),
        [&playerPos](const std::shared_ptr<Chunk>& a, const std::shared_ptr<Chunk>& b) {
            glm::vec3 daVec = a->getCenter() - playerPos;
            glm::vec3 dbVec = b->getCenter() - playerPos;
            float da = glm::dot(daVec, daVec);
            float db = glm::dot(dbVec, dbVec);
            return da < db;
        });
}

// ============================================================================
// 磁盘缓存实现
// ============================================================================

std::string World::worldCachePath()
{
    return "world_cache";
}

void World::saveChunkToDisk(const glm::ivec3& pos, const uint16_t* blocks, const uint8_t* meta)
{
    std::string dir = worldCachePath();
    std::string filename = dir + "/" +
        std::to_string(pos.x) + "_" +
        std::to_string(pos.y) + "_" +
        std::to_string(pos.z) + ".chunk";

    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return;
    ofs.write(reinterpret_cast<const char*>(blocks), CHUNK_VOLUME * sizeof(uint16_t));
    ofs.write(reinterpret_cast<const char*>(meta),   CHUNK_VOLUME * sizeof(uint8_t));
}

bool World::loadChunkFromDisk(const glm::ivec3& pos, uint16_t* blocks, uint8_t* meta)
{
    std::string filename = worldCachePath() + "/" +
        std::to_string(pos.x) + "_" +
        std::to_string(pos.y) + "_" +
        std::to_string(pos.z) + ".chunk";

    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) return false;
    ifs.read(reinterpret_cast<char*>(blocks), CHUNK_VOLUME * sizeof(uint16_t));
    ifs.read(reinterpret_cast<char*>(meta),   CHUNK_VOLUME * sizeof(uint8_t));
    return true;
}

// ============================================================================

void World::generateInitialChunks(const glm::vec3& centerPos, int radius)
{
    glm::ivec3 centerChunk = Chunk::worldToChunk(centerPos);
    std::cout << "[World] 同步生成初始区块，中心: ("
              << centerChunk.x << ", " << centerChunk.y << ", " << centerChunk.z
              << "), 半径: " << radius << std::endl;

    for (int dx = -radius; dx <= radius; ++dx)
    {
        for (int dz = -radius; dz <= radius; ++dz)
        {
            // 只生成地表附近和地下的区块（y 方向有限范围）
            for (int dy = -2; dy <= 1; ++dy)
            {
                glm::ivec3 chunkPos(centerChunk.x + dx,
                                    centerChunk.y + dy,
                                    centerChunk.z + dz);

                uint64_t key = Chunk::chunkKey(chunkPos.x, chunkPos.y, chunkPos.z);
                if (m_loadedChunkKeys.find(key) != m_loadedChunkKeys.end())
                    continue;

                m_loadedChunkKeys.insert(key);

                // 同步生成地形
                auto chunk = Chunk::getOrCreateChunk(chunkPos);
                chunk->setState(ChunkState::GENERATING);
                m_terrainGen.generate(*chunk);

                // 同步构建网格
                chunk->setState(ChunkState::MESHING);
                LODMeshData meshes[3];
                MeshBuilder::buildAllLODs(*chunk, meshes);
                for (int i = 0; i < 3; ++i)
                {
                    chunk->getMeshData(static_cast<LODLevel>(i)) = std::move(meshes[i]);
                }
                chunk->setState(ChunkState::READY);
                chunk->setMeshDirty(false);

                // 插入八叉树
                m_octree.insert(chunk);
            }
        }
    }

    m_loadedChunks.store(m_loadedChunkKeys.size());
    std::cout << "[World] 初始区块同步完成，共 " << m_loadedChunks.load() << " 个区块" << std::endl;
}

void World::generateChunkAsync(const glm::ivec3& chunkPos, ThreadPool::Priority priority)
{
    uint64_t key = Chunk::chunkKey(chunkPos.x, chunkPos.y, chunkPos.z);

    // 标记为已加载
    m_loadedChunkKeys.insert(key);
    m_pendingGenerate++;

    // 异步生成地形
    m_threadPool.enqueue(priority,
        [this, chunkPos, key]()
        {
            auto chunk = Chunk::getOrCreateChunk(chunkPos);
            chunk->setState(ChunkState::GENERATING);

            // 尝试从磁盘缓存加载
            bool loaded = loadChunkFromDisk(chunkPos, chunk->data(), chunk->metaData());
            if (!loaded)
            {
                // 磁盘无缓存 → 生成
                m_terrainGen.generate(*chunk);
                // 保存到磁盘缓存
                saveChunkToDisk(chunkPos, chunk->data(), chunk->metaData());
            }

            m_pendingGenerate--;
            m_pendingMesh++;

            // 线程安全：将区块加入待插入队列，由主线程在 flushPendingInserts() 中插入八叉树
            {
                std::lock_guard<std::mutex> lock(m_pendingInsertsMutex);
                m_pendingOctreeInserts.push_back(chunk);
            }

            // 使用普通优先级构建网格
            // 注意：内层 lambda 也需要捕获 this 以访问 m_pendingMesh
            m_threadPool.enqueue(ThreadPool::Priority::NORMAL,
                [this, chunk]()
                {
                    chunk->setState(ChunkState::MESHING);
                    // 构建所有 LOD 级别的网格
                    LODMeshData meshes[3];
                    MeshBuilder::buildAllLODs(*chunk, meshes);
                    for (int i = 0; i < 3; ++i)
                    {
                        chunk->getMeshData(static_cast<LODLevel>(i)) = std::move(meshes[i]);
                    }
                    chunk->setState(ChunkState::READY);
                    chunk->setMeshDirty(false);
                    m_pendingMesh--;
                });
        });
}

void World::meshChunkAsync(const std::shared_ptr<Chunk>& chunk, ThreadPool::Priority priority)
{
    // 直接在 generateChunkAsync 中链式调用，此方法作为备用
    chunk->setState(ChunkState::MESHING);
    m_pendingMesh++;

    m_threadPool.enqueue(priority,
        [this, chunk]()
        {
            LODMeshData meshes[3];
            MeshBuilder::buildAllLODs(*chunk, meshes);
            for (int i = 0; i < 3; ++i)
            {
                chunk->getMeshData(static_cast<LODLevel>(i)) = std::move(meshes[i]);
            }
            chunk->setState(ChunkState::READY);
            chunk->setMeshDirty(false);
            m_pendingMesh--;
        });
}

void World::unloadChunk(const glm::ivec3& chunkPos)
{
    m_octree.remove(chunkPos);
    Chunk::removeChunk(chunkPos);
}

ThreadPool::Priority World::getPriority(
    const glm::ivec3& chunkPos,
    const glm::ivec3& playerChunk) const
{
    int dx = chunkPos.x - playerChunk.x;
    int dy = chunkPos.y - playerChunk.y;
    int dz = chunkPos.z - playerChunk.z;
    float dist = std::sqrt(static_cast<float>(dx*dx + dy*dy + dz*dz));

    // 根据距离决定优先级
    if (dist <= 3.0f)
        return ThreadPool::Priority::HIGH;
    else if (dist <= 6.0f)
        return ThreadPool::Priority::NORMAL;
    else
        return ThreadPool::Priority::LOW;
}
