/**
 * terrain_generator.cpp —— 地形生成器实现
 *
 * 【生成算法】
 * 对区块中的每个方块坐标：
 * 1. 计算 2D 高度图噪声 → 获取该 (x,z) 处的地表高度
 * 2. 如果 y < 高度，填充为地下方块
 * 3. 如果 y == 高度，填充为地表方块
 * 4. 如果 y > 高度且 y < 海平面，填充为水
 * 5. 应用 3D 洞穴噪声：在特定条件下的地下方块转为空气
 * 6. 最底层填充基岩
 */
#include "terrain_generator.h"
#include "block_registry.h"

#include <algorithm>
#include <cmath>

TerrainGenerator::TerrainGenerator(uint32_t seed)
    : m_seed(seed)
    , m_amplitude(40.0f)
    , m_frequency(0.01f)
    , m_octaves(4)
    , m_lacunarity(2.0f)
    , m_persistence(0.5f)
    , m_caveThreshold(0.15f)
    , m_caveFrequency(0.08f)
    , m_caveOctaves(2)
    , m_seaLevel(32)
    , m_heightNoise(seed)
    , m_caveNoise(seed + 1)
    , m_detailNoise(seed + 2)
{
}

void TerrainGenerator::generate(Chunk& chunk)
{
    const auto& registry = BlockRegistry::instance();
    int baseX = chunk.m_position.x * CHUNK_SIZE;
    int baseY = chunk.m_position.y * CHUNK_SIZE;
    int baseZ = chunk.m_position.z * CHUNK_SIZE;

    uint16_t airId   = static_cast<uint16_t>(BlockID::AIR);
    uint16_t grassId = static_cast<uint16_t>(BlockID::GRASS);
    uint16_t dirtId  = static_cast<uint16_t>(BlockID::DIRT);
    uint16_t stoneId = static_cast<uint16_t>(BlockID::STONE);
    uint16_t sandId  = static_cast<uint16_t>(BlockID::SAND);
    uint16_t waterId = static_cast<uint16_t>(BlockID::WATER);
    uint16_t bedrockId = static_cast<uint16_t>(BlockID::BEDROCK);

    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
        int wy = baseY + y;
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
            int wz = baseZ + z;
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
                int wx = baseX + x;

                // 获取该 (x,z) 位置的地表高度
                int height = getHeightAt(wx, wz);
                uint16_t blockType = airId;

                if (wy <= 0)
                {
                    // 最底层：基岩
                    blockType = bedrockId;
                }
                else if (wy < height)
                {
                    // 地下
                    if (wy < height - 4)
                    {
                        // 深层：石头
                        blockType = stoneId;

                        // 洞穴生成：使用 3D 噪声打洞
                        if (isCave(wx, wy, wz))
                            blockType = airId;
                    }
                    else
                    {
                        // 近地表层：泥土
                        blockType = dirtId;
                    }
                }
                else if (wy == height)
                {
                    // 地表层
                    if (height <= m_seaLevel + 1)
                        blockType = sandId; // 海滩/河床
                    else
                        blockType = grassId;
                }
                else if (wy <= m_seaLevel)
                {
                    // 水下
                    blockType = waterId;
                }
                // else: 空气

                chunk.data()[Chunk::index(x, y, z)] = blockType;
            }
        }
    }

    chunk.setState(ChunkState::GENERATED);
    chunk.setMeshDirty(true);
}

int TerrainGenerator::getHeightAt(int wx, int wz) const
{
    // 使用 fBm 生成高度图
    double noiseVal = m_heightNoise.fbm2D(
        wx * m_frequency, wz * m_frequency,
        m_octaves, m_lacunarity, m_persistence);

    // 将 [-1, 1] 映射到 [baseLevel, baseLevel + amplitude]
    int baseLevel = m_seaLevel + 5;
    int height = baseLevel + static_cast<int>(noiseVal * m_amplitude);

    // 添加细节噪声使地形更自然
    double detail = m_detailNoise.noise2D(wx * 0.05, wz * 0.05) * 3.0;
    height += static_cast<int>(detail);

    return std::max(1, height);
}

bool TerrainGenerator::isCave(int wx, int wy, int wz) const
{
    // 使用 3D 噪声确定洞穴区域
    double caveNoise = m_caveNoise.fbm3D(
        wx * m_caveFrequency,
        wy * m_caveFrequency,
        wz * m_caveFrequency,
        m_caveOctaves, 2.0f, 0.5f);

    // 噪声值高于阈值 → 洞穴
    return caveNoise > m_caveThreshold;
}

void TerrainGenerator::setSeed(uint32_t seed)
{
    m_seed = seed;
    m_heightNoise = PerlinNoise(seed);
    m_caveNoise = PerlinNoise(seed + 1);
    m_detailNoise = PerlinNoise(seed + 2);
}
