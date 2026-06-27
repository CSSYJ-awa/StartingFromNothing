/**
 * block_registry.h —— 方块注册表
 *
 * 管理游戏中所有方块类型的定义。支持运行时动态注册新方块。
 * 每个方块使用 uint16_t 类型的 ID 标识（支持最多 65535 种方块）。
 * ID 0 保留给空气（AIR）。
 *
 * 【紧凑存储格式】
 * 区块中每个方块使用：
 *   - uint16_t: 方块类型 ID（2 字节）
 *   - uint8_t:  元数据（1 字节，如亮度、旋转、水分等）
 * 每个方块仅占用 3 字节，16×16×16 区块 = 12 KB。
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

// ============================================================================
// 预定义方块 ID
// ============================================================================
enum class BlockID : uint16_t
{
    AIR      = 0,
    GRASS    = 1,
    DIRT     = 2,
    STONE    = 3,
    SAND     = 4,
    WOOD     = 5,
    LEAVES   = 6,
    WATER    = 7,
    BEDROCK  = 8,
    // 可继续扩展...
    CUSTOM_START = 256 // 自定义方块起始 ID
};

// ============================================================================
// 方块面枚举
// ============================================================================
enum class BlockFace : uint8_t
{
    TOP    = 0,
    BOTTOM = 1,
    FRONT  = 2,
    BACK   = 3,
    LEFT   = 4,
    RIGHT  = 5,
    COUNT  = 6
};

// ============================================================================
// BlockData —— 方块属性定义
// ============================================================================
struct BlockData
{
    std::string  name;                    // 方块名称
    bool         isSolid      = true;     // 是否为固体（参与碰撞）
    bool         isTransparent = false;   // 是否透明（相邻透明面不剔除）
    bool         isOpaque     = true;     // 是否不透明（用于光线计算）
    float        hardness     = 1.0f;     // 硬度（挖掘时间倍数）
    glm::vec4    color        = glm::vec4(1.0f); // 方块的程序化颜色 (RGBA)
    glm::ivec2   textureAtlasPos = glm::ivec2(0); // 纹理图集中的位置（预留）
};

// ============================================================================
// BlockRegistry —— 方块注册表（单例）
// ============================================================================
class BlockRegistry
{
public:
    /** 获取全局单例 */
    static BlockRegistry& instance()
    {
        static BlockRegistry registry;
        return registry;
    }

    /**
     * 注册一个新方块类型
     * @param id   方块 ID（不能为 0，AIR 保留）
     * @param data 方块属性
     * @return 是否成功注册（false 表示 ID 已存在）
     */
    bool registerBlock(BlockID id, const BlockData& data)
    {
        uint16_t idVal = static_cast<uint16_t>(id);
        if (idVal == 0 || m_blocks.find(idVal) != m_blocks.end())
            return false;
        m_blocks[idVal] = data;
        return true;
    }

    /**
     * 动态注册一个新方块（自动分配 ID）
     * @param data 方块属性
     * @return 分配的 BlockID
     */
    BlockID registerDynamicBlock(const BlockData& data)
    {
        uint16_t id = m_nextDynamicId++;
        m_blocks[id] = data;
        return static_cast<BlockID>(id);
    }

    /** 获取方块数据 */
    const BlockData& getBlock(BlockID id) const
    {
        return getBlock(static_cast<uint16_t>(id));
    }

    const BlockData& getBlock(uint16_t id) const
    {
        static BlockData s_airBlock = {
            "Air", false, true, false, 0.0f,
            glm::vec4(0.0f), glm::ivec2(0)
        };

        auto it = m_blocks.find(id);
        if (it != m_blocks.end())
            return it->second;
        return s_airBlock; // 未知 ID 返回空气
    }

    /** 判断方块是否为空气 */
    static bool isAir(uint16_t id) { return id == 0; }

    /** 判断方块是否透明 */
    bool isTransparent(uint16_t id) const
    {
        return id == 0 || getBlock(id).isTransparent;
    }

    /** 判断方块是否为固体 */
    bool isSolid(uint16_t id) const
    {
        return id != 0 && getBlock(id).isSolid;
    }

    /** 获取已注册的方块数量 */
    size_t blockCount() const { return m_blocks.size(); }

    /** 初始化默认方块 */
    void initDefaults();

private:
    BlockRegistry()
    {
        initDefaults();
    }

    std::unordered_map<uint16_t, BlockData> m_blocks;
    uint16_t m_nextDynamicId = static_cast<uint16_t>(BlockID::CUSTOM_START);
};

// ============================================================================
// 内联实现：初始化默认方块
// ============================================================================
inline void BlockRegistry::initDefaults()
{
    // 空气（不可见、不可碰撞）
    registerBlock(BlockID::AIR, {
        "Air", false, true, false, 0.0f,
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)
    });

    // 草方块
    registerBlock(BlockID::GRASS, {
        "Grass", true, false, true, 0.6f,
        glm::vec4(0.27f, 0.62f, 0.16f, 1.0f) // 绿色
    });

    // 泥土
    registerBlock(BlockID::DIRT, {
        "Dirt", true, false, true, 0.5f,
        glm::vec4(0.55f, 0.40f, 0.22f, 1.0f) // 棕色
    });

    // 石头
    registerBlock(BlockID::STONE, {
        "Stone", true, false, true, 1.5f,
        glm::vec4(0.55f, 0.55f, 0.55f, 1.0f) // 灰色
    });

    // 沙子
    registerBlock(BlockID::SAND, {
        "Sand", true, false, true, 0.5f,
        glm::vec4(0.76f, 0.72f, 0.50f, 1.0f) // 浅黄色
    });

    // 木头
    registerBlock(BlockID::WOOD, {
        "Wood", true, false, true, 1.0f,
        glm::vec4(0.45f, 0.28f, 0.14f, 1.0f) // 深棕色
    });

    // 树叶（透明）
    registerBlock(BlockID::LEAVES, {
        "Leaves", true, true, false, 0.2f,
        glm::vec4(0.20f, 0.50f, 0.12f, 0.8f) // 深绿色半透明
    });

    // 水（透明、非固体）
    registerBlock(BlockID::WATER, {
        "Water", false, true, false, 0.0f,
        glm::vec4(0.20f, 0.40f, 0.80f, 0.5f) // 蓝色半透明
    });

    // 基岩
    registerBlock(BlockID::BEDROCK, {
        "Bedrock", true, false, true, 999.0f,
        glm::vec4(0.20f, 0.20f, 0.20f, 1.0f) // 深黑色
    });
}
