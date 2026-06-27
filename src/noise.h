/**
 * noise.h —— Perlin/Simplex 噪声实现（纯头文件）
 *
 * 提供多维噪声函数，用于体素世界的地形生成。
 * 实现基于经典的 Perlin 噪声算法，支持 2D/3D 噪声。
 */
#pragma once

#include <cstdint>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

// ============================================================================
// PerlinNoise —— 改进的 Perlin 噪声（Ken Perlin, 2002）
// ============================================================================
class PerlinNoise
{
public:
    /**
     * 构造噪声生成器
     * @param seed 随机种子
     */
    explicit PerlinNoise(uint32_t seed = 42)
    {
        m_perm.resize(512);
        std::vector<int> p(256);
        std::iota(p.begin(), p.end(), 0);

        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);

        for (int i = 0; i < 256; ++i)
        {
            m_perm[i] = p[i];
            m_perm[i + 256] = p[i];
        }
    }

    /**
     * 2D Perlin 噪声
     * @param x X 坐标
     * @param y Y 坐标
     * @return [-1, 1] 范围内的噪声值
     */
    double noise2D(double x, double y) const
    {
        // 找到包含点的单位方格
        int xi = static_cast<int>(std::floor(x)) & 255;
        int yi = static_cast<int>(std::floor(y)) & 255;

        // 计算局部坐标
        double xf = x - std::floor(x);
        double yf = y - std::floor(y);

        // 平滑插值曲线
        double u = fade(xf);
        double v = fade(yf);

        // 哈希四个角
        int aa = m_perm[m_perm[xi] + yi];
        int ab = m_perm[m_perm[xi] + yi + 1];
        int ba = m_perm[m_perm[xi + 1] + yi];
        int bb = m_perm[m_perm[xi + 1] + yi + 1];

        // 双线性插值梯度方向
        double x1 = lerp(grad2D(aa, xf, yf), grad2D(ba, xf - 1.0, yf), u);
        double x2 = lerp(grad2D(ab, xf, yf - 1.0), grad2D(bb, xf - 1.0, yf - 1.0), u);
        return lerp(x1, x2, v);
    }

    /**
     * 3D Perlin 噪声
     * @param x X 坐标
     * @param y Y 坐标
     * @param z Z 坐标
     * @return [-1, 1] 范围内的噪声值
     */
    double noise3D(double x, double y, double z) const
    {
        int xi = static_cast<int>(std::floor(x)) & 255;
        int yi = static_cast<int>(std::floor(y)) & 255;
        int zi = static_cast<int>(std::floor(z)) & 255;

        double xf = x - std::floor(x);
        double yf = y - std::floor(y);
        double zf = z - std::floor(z);

        double u = fade(xf);
        double v = fade(yf);
        double w = fade(zf);

        int a  = m_perm[xi] + yi;
        int aa = m_perm[a] + zi;
        int ab = m_perm[a + 1] + zi;
        int b  = m_perm[xi + 1] + yi;
        int ba = m_perm[b] + zi;
        int bb = m_perm[b + 1] + zi;

        double x1 = lerp(grad3D(m_perm[aa], xf, yf, zf), grad3D(m_perm[ba], xf - 1.0, yf, zf), u);
        double x2 = lerp(grad3D(m_perm[ab], xf, yf - 1.0, zf), grad3D(m_perm[bb], xf - 1.0, yf - 1.0, zf), u);
        double y1 = lerp(x1, x2, v);

        x1 = lerp(grad3D(m_perm[aa + 1], xf, yf, zf - 1.0), grad3D(m_perm[ba + 1], xf - 1.0, yf, zf - 1.0), u);
        x2 = lerp(grad3D(m_perm[ab + 1], xf, yf - 1.0, zf - 1.0), grad3D(m_perm[bb + 1], xf - 1.0, yf - 1.0, zf - 1.0), u);
        double y2 = lerp(x1, x2, v);

        return lerp(y1, y2, w);
    }

    /**
     * 分形布朗运动（fBm）—— 多层噪声叠加
     * @param octaves 倍频程数
     * @param lacunarity 频率倍增因子
     * @param persistence 幅度衰减因子
     */
    double fbm2D(double x, double y, int octaves = 4,
                  double lacunarity = 2.0, double persistence = 0.5) const
    {
        double value = 0.0;
        double amplitude = 1.0;
        double frequency = 1.0;
        double maxValue = 0.0;

        for (int i = 0; i < octaves; ++i)
        {
            value += amplitude * noise2D(x * frequency, y * frequency);
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return value / maxValue; // 归一化到 [-1, 1]
    }

    /**
     * 3D 分形布朗运动
     */
    double fbm3D(double x, double y, double z, int octaves = 4,
                  double lacunarity = 2.0, double persistence = 0.5) const
    {
        double value = 0.0;
        double amplitude = 1.0;
        double frequency = 1.0;
        double maxValue = 0.0;

        for (int i = 0; i < octaves; ++i)
        {
            value += amplitude * noise3D(x * frequency, y * frequency, z * frequency);
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return value / maxValue;
    }

private:
    std::vector<int> m_perm; // 排列表（大小为 512）

    static double fade(double t)
    {
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }

    static double lerp(double a, double b, double t)
    {
        return a + t * (b - a);
    }

    static double grad2D(int hash, double x, double y)
    {
        int h = hash & 3;
        double u = (h < 2) ? x : y;
        double v = (h < 2) ? y : x;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    static double grad3D(int hash, double x, double y, double z)
    {
        int h = hash & 15;
        double u = (h < 8) ? x : y;
        double v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
};

// ============================================================================
// SimplexNoise —— 简单高效的 Simplex 噪声（简化版）
// 适用于需要更高维度和更好视觉质量的场景
// ============================================================================
class SimplexNoise
{
public:
    explicit SimplexNoise(uint32_t seed = 42)
        : m_perm(512)
    {
        std::vector<int> p(256);
        std::iota(p.begin(), p.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);
        for (int i = 0; i < 256; ++i)
        {
            m_perm[i] = p[i];
            m_perm[i + 256] = p[i];
        }
    }

    /**
     * 2D Simplex 噪声
     */
    double noise2D(double x, double y) const
    {
        // 简化的 2D Simplex 噪声实现
        // 使用与 Perlin 相同的接口，但内部使用 Simplex 网格
        const double F2 = 0.5 * (std::sqrt(3.0) - 1.0);
        const double G2 = (3.0 - std::sqrt(3.0)) / 6.0;

        double s = (x + y) * F2;
        int i = static_cast<int>(std::floor(x + s));
        int j = static_cast<int>(std::floor(y + s));
        double t = (i + j) * G2;
        double X0 = i - t;
        double Y0 = j - t;
        double x0 = x - X0;
        double y0 = y - Y0;

        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else { i1 = 0; j1 = 1; }

        double x1 = x0 - i1 + G2;
        double y1 = y0 - j1 + G2;
        double x2 = x0 - 1.0 + 2.0 * G2;
        double y2 = y0 - 1.0 + 2.0 * G2;

        int ii = i & 255;
        int jj = j & 255;
        int gi0 = m_perm[ii + m_perm[jj]] % 12;
        int gi1 = m_perm[ii + i1 + m_perm[jj + j1]] % 12;
        int gi2 = m_perm[ii + 1 + m_perm[jj + 1]] % 12;

        double n0 = 0, n1 = 0, n2 = 0;

        double t0 = 0.5 - x0 * x0 - y0 * y0;
        if (t0 > 0)
        {
            t0 *= t0;
            n0 = t0 * t0 * grad(gi0, x0, y0);
        }
        double t1 = 0.5 - x1 * x1 - y1 * y1;
        if (t1 > 0)
        {
            t1 *= t1;
            n1 = t1 * t1 * grad(gi1, x1, y1);
        }
        double t2 = 0.5 - x2 * x2 - y2 * y2;
        if (t2 > 0)
        {
            t2 *= t2;
            n2 = t2 * t2 * grad(gi2, x2, y2);
        }

        return 70.0 * (n0 + n1 + n2);
    }

private:
    std::vector<int> m_perm;

    static double grad(int hash, double x, double y)
    {
        int h = hash & 3;
        double u = (h < 2) ? x : y;
        double v = (h < 2) ? y : x;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
};
