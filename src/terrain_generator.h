/**
 * terrain_generator.h —— 地形生成器
 *
 * 使用 Perlin 噪声生成无限地形，支持：
 * 1. 高度图（基于 2D 噪声）
 * 2. 洞穴/矿脉（基于 3D 噪声）
 * 3. 生态域（基于温度/湿度噪声，预留）
 *
 * 【生成策略】
 * 1. 使用 fBm（分形布朗运动）叠加多层噪声获得自然地形起伏
 * 2. 高度图决定地表 Y 坐标
 * 3. 3D 洞穴噪声决定地下空洞
 * 4. 根据高度分层填充不同方块类型
 */
#pragma once

#include "chunk.h"
#include "noise.h"

#include <memory>
#include <glm/glm.hpp>

// ============================================================================
// TerrainGenerator —— 地形生成器
// ============================================================================
class TerrainGenerator
{
public:
    TerrainGenerator(uint32_t seed = 42);

    /**
     * 为指定的区块生成地形数据
     * 直接填充区块的方块数组
     * @param chunk 目标区块
     */
    void generate(Chunk& chunk);

    /**
     * 获取某个世界坐标处的地表高度
     * @param wx 世界 X 坐标
     * @param wz 世界 Z 坐标
     * @return 地表 Y 坐标
     */
    int getHeightAt(int wx, int wz) const;

    /**
     * 判断某个世界坐标处的方块是否为洞穴
     * @param wx, wy, wz 世界坐标
     * @return true 表示为洞穴（空气）
     */
    bool isCave(int wx, int wy, int wz) const;

    /** 设置生成参数 */
    void setSeed(uint32_t seed);
    void setAmplitude(float amp) { m_amplitude = amp; }
    void setFrequency(float freq) { m_frequency = freq; }
    void setCaveThreshold(float threshold) { m_caveThreshold = threshold; }

private:
    uint32_t m_seed;

    // 地形参数
    float m_amplitude;       // 地形起伏幅度
    float m_frequency;       // 噪声频率
    int   m_octaves;         // fBm 倍频程数
    float m_lacunarity;      // 频率倍增
    float m_persistence;     // 幅度衰减

    // 洞穴参数
    float m_caveThreshold;   // 洞穴阈值（噪声 > 此值 → 洞穴）
    float m_caveFrequency;   // 洞穴噪声频率
    int   m_caveOctaves;     // 洞穴噪声倍频程

    // 海平面高度
    int   m_seaLevel;

    PerlinNoise m_heightNoise; // 高度图噪声
    PerlinNoise m_caveNoise;   // 洞穴噪声
    PerlinNoise m_detailNoise; // 细节噪声
};
