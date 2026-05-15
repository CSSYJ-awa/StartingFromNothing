// =============================================================================
// Voxel Sandbox Game (类似 Minecraft 的沙盒游戏)
// 功能：地形生成、玩家移动、碰撞检测
// 技术栈：Vulkan + GLFW + GLM
// =============================================================================

#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "vert_spv.h"
#include "frag_spv.h"

// =============================================================================
// 全局配置
// =============================================================================

constexpr int CHUNK_SIZE = 32;           // 区块大小
constexpr int WORLD_WIDTH = 4;           // 世界宽度（区块数）
constexpr int WORLD_HEIGHT = 4;          // 世界高度（区块数）
constexpr int WORLD_DEPTH = 4;           // 世界深度（区块数）
constexpr float BLOCK_SIZE = 1.0f;       // 方块大小
constexpr float PLAYER_SIZE = 0.6f;      // 玩家碰撞盒大小
constexpr float PLAYER_HEIGHT = 1.8f;    // 玩家高度
constexpr float GRAVITY = 9.8f;          // 重力加速度
constexpr float JUMP_FORCE = 5.0f;       // 跳跃力
constexpr float MOVE_SPEED = 5.0f;       // 移动速度

// 字体渲染相关
FT_Library g_ftLibrary = nullptr;
FT_Face g_ftFace = nullptr;

// 将UTF-8字符转换为Unicode码点
unsigned int utf8_to_unicode(const char* utf8_str) {
    unsigned char c = (unsigned char)utf8_str[0];
    unsigned int unicode = 0;
    if (c < 0x80) {
        unicode = c;
    } else if ((c & 0xE0) == 0xC0) {
        unicode = ((c & 0x1F) << 6) | (utf8_str[1] & 0x3F);
    } else if ((c & 0xF0) == 0xE0) {
        unicode = ((c & 0x0F) << 12) | ((utf8_str[1] & 0x3F) << 6) | (utf8_str[2] & 0x3F);
    } else if ((c & 0xF8) == 0xF0) {
        unicode = ((c & 0x07) << 18) | ((utf8_str[1] & 0x3F) << 12) | ((utf8_str[2] & 0x3F) << 6) | (utf8_str[3] & 0x3F);
    }
    return unicode;
}

// 渲染文字到顶点数据
void renderTextToVertices(const char* text, float x, float y, float scale, std::vector<float>& vertices) {
    if (!g_ftFace) return;
    
    const char* p = text;
    float currentX = x;
    
    while (*p) {
        unsigned int unicode = utf8_to_unicode(p);
        FT_UInt glyph_index = FT_Get_Char_Index(g_ftFace, unicode);
        
        if (glyph_index == 0) {
            p += (unicode < 0x80) ? 1 : (unicode < 0x800) ? 2 : (unicode < 0x10000) ? 3 : 4;
            continue;
        }
        
        if (FT_Load_Glyph(g_ftFace, glyph_index, FT_LOAD_RENDER)) {
            p += (unicode < 0x80) ? 1 : (unicode < 0x800) ? 2 : (unicode < 0x10000) ? 3 : 4;
            continue;
        }
        
        FT_Bitmap* bitmap = &g_ftFace->glyph->bitmap;
        FT_GlyphSlot slot = g_ftFace->glyph;
        
        int width = bitmap->width;
        int height = bitmap->rows;
        int pitch = bitmap->pitch;
        
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                unsigned char pixel = bitmap->buffer[row * pitch + col];
                if (pixel > 128) {
                    float px = currentX + (col + slot->bitmap_left) * scale;
                    float py = y - (row - (height - slot->bitmap_top)) * scale;
                    float pw = scale;
                    float ph = scale;
                    
                    // 白色文字，z=-0.1确保在按钮前面
                    float v[] = {
                        px, py + ph, -0.1f,   0.5f, 1.0f, 0.3f,   1.0f, 1.0f, 1.0f,
                        px + pw, py + ph, -0.1f,   0.5f, 1.0f, 0.3f,   1.0f, 1.0f, 1.0f,
                        px + pw, py, -0.1f,   0.5f, 1.0f, 0.3f,   1.0f, 1.0f, 1.0f,
                        px, py + ph, -0.1f,   0.5f, 1.0f, 0.3f,   1.0f, 1.0f, 1.0f,
                        px + pw, py, -0.1f,   0.5f, 1.0f, 0.3f,   1.0f, 1.0f, 1.0f,
                        px, py, -0.1f,   0.5f, 1.0f, 0.3f,   1.0f, 1.0f, 1.0f
                    };
                    vertices.insert(vertices.end(), v, v + sizeof(v)/sizeof(float));
                }
            }
        }
        
        currentX += slot->advance.x / 64.0f * scale;
        p += (unicode < 0x80) ? 1 : (unicode < 0x800) ? 2 : (unicode < 0x10000) ? 3 : 4;
    }
}


// =============================================================================
// 方块类型
// =============================================================================

enum class BlockType {
    AIR,
    GRASS,
    DIRT,
    STONE,
    WOOD,
    LEAVES,
    WATER,
    SAND,
    COBBLESTONE,
    BRICK
};

// 方块颜色映射
const std::array<glm::vec3, 10> BLOCK_COLORS = {
    glm::vec3(0.0f, 0.0f, 0.0f),       // AIR
    glm::vec3(0.3f, 0.8f, 0.3f),       // GRASS
    glm::vec3(0.6f, 0.4f, 0.2f),       // DIRT
    glm::vec3(0.5f, 0.5f, 0.5f),       // STONE
    glm::vec3(0.6f, 0.4f, 0.2f),       // WOOD
    glm::vec3(0.2f, 0.6f, 0.3f),       // LEAVES
    glm::vec3(0.2f, 0.4f, 0.8f),       // WATER
    glm::vec3(0.9f, 0.8f, 0.6f),       // SAND
    glm::vec3(0.4f, 0.4f, 0.4f),       // COBBLESTONE
    glm::vec3(0.7f, 0.3f, 0.3f)        // BRICK
};

// =============================================================================
// Perlin 噪声生成器（用于地形生成）
// =============================================================================

class PerlinNoise {
public:
    PerlinNoise(uint32_t seed = 12345) {
        // 初始化置换表
        for (int i = 0; i < 256; ++i) {
            p[i] = i;
        }
        // Fisher-Yates 洗牌
        for (int i = 255; i > 0; --i) {
            std::swap(p[i], p[seed % (i + 1)]);
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        }
        // 复制到扩展表
        for (int i = 0; i < 512; ++i) {
            p[256 + i] = p[i];
        }
    }

    float noise(float x, float y, float z) const {
        // 找到单元立方体
        int X = (int)floor(x) & 255;
        int Y = (int)floor(y) & 255;
        int Z = (int)floor(z) & 255;

        // 计算单元内的位置
        x -= floor(x);
        y -= floor(y);
        z -= floor(z);

        // 计算平滑曲线值
        float u = fade(x);
        float v = fade(y);
        float w = fade(z);

        // 获取八个角的哈希值
        int A = p[X] + Y;
        int AA = p[A] + Z;
        int AB = p[A + 1] + Z;
        int B = p[X + 1] + Y;
        int BA = p[B] + Z;
        int BB = p[B + 1] + Z;

        // 插值
        return lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z),
                                       grad(p[BA], x - 1, y, z)),
                               lerp(u, grad(p[AB], x, y - 1, z),
                                       grad(p[BB], x - 1, y - 1, z))),
                       lerp(v, lerp(u, grad(p[AA + 1], x, y, z - 1),
                                       grad(p[BA + 1], x - 1, y, z - 1)),
                               lerp(u, grad(p[AB + 1], x, y - 1, z - 1),
                                       grad(p[BB + 1], x - 1, y - 1, z - 1))));
    }

    float octaveNoise(float x, float y, float z, int octaves, float persistence = 0.5f) const {
        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; ++i) {
            total += noise(x * frequency, y * frequency, z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }

private:
    float fade(float t) const { return t * t * t * (t * (t * 6 - 15) + 10); }
    float lerp(float t, float a, float b) const { return a + t * (b - a); }
    float grad(int hash, float x, float y, float z) const {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    int p[512];
};

// =============================================================================
// 区块类
// =============================================================================

struct Chunk {
    int x, y, z;
    std::vector<BlockType> blocks;
    bool needsRebuild = true;

    Chunk(int cx, int cy, int cz) : x(cx), y(cy), z(cz) {
        blocks.resize(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, BlockType::AIR);
    }

    BlockType getBlock(int bx, int by, int bz) const {
        if (bx < 0 || bx >= CHUNK_SIZE || by < 0 || by >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) {
            return BlockType::AIR;
        }
        return blocks[by * CHUNK_SIZE * CHUNK_SIZE + bz * CHUNK_SIZE + bx];
    }

    void setBlock(int bx, int by, int bz, BlockType type) {
        if (bx >= 0 && bx < CHUNK_SIZE && by >= 0 && by < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE) {
            blocks[by * CHUNK_SIZE * CHUNK_SIZE + bz * CHUNK_SIZE + bx] = type;
            needsRebuild = true;
        }
    }
};

// =============================================================================
// 世界类
// =============================================================================

class VoxelWorld {
public:
    VoxelWorld() : noise(12345) {
        generateTerrain();
    }

    BlockType getBlock(int x, int y, int z) const {
        // 边界检查
        if (y < 0) return BlockType::STONE;
        if (y >= WORLD_HEIGHT * CHUNK_SIZE) return BlockType::AIR;

        // 计算区块坐标
        int cx = x / CHUNK_SIZE;
        int cy = y / CHUNK_SIZE;
        int cz = z / CHUNK_SIZE;

        // 检查区块是否存在
        auto key = std::make_tuple(cx, cy, cz);
        if (chunks.find(key) == chunks.end()) {
            return BlockType::AIR;
        }

        // 计算区块内坐标
        int bx = x % CHUNK_SIZE;
        int by = y % CHUNK_SIZE;
        int bz = z % CHUNK_SIZE;

        if (bx < 0) bx += CHUNK_SIZE;
        if (by < 0) by += CHUNK_SIZE;
        if (bz < 0) bz += CHUNK_SIZE;

        return chunks.at(key)->getBlock(bx, by, bz);
    }

    void setBlock(int x, int y, int z, BlockType type) {
        int cx = x / CHUNK_SIZE;
        int cy = y / CHUNK_SIZE;
        int cz = z / CHUNK_SIZE;

        auto key = std::make_tuple(cx, cy, cz);
        if (chunks.find(key) != chunks.end()) {
            int bx = x % CHUNK_SIZE;
            int by = y % CHUNK_SIZE;
            int bz = z % CHUNK_SIZE;

            if (bx < 0) bx += CHUNK_SIZE;
            if (by < 0) by += CHUNK_SIZE;
            if (bz < 0) bz += CHUNK_SIZE;

            chunks.at(key)->setBlock(bx, by, bz, type);
        }
    }

    // 生成地形
    void generateTerrain() {
        for (int cx = 0; cx < WORLD_WIDTH; ++cx) {
            for (int cz = 0; cz < WORLD_DEPTH; ++cz) {
                for (int cy = 0; cy < WORLD_HEIGHT; ++cy) {
                    auto chunk = std::make_unique<Chunk>(cx, cy, cz);
                    generateChunk(chunk.get());
                    chunks[std::make_tuple(cx, cy, cz)] = std::move(chunk);
                }
            }
        }
    }

    // 生成单个区块
    void generateChunk(Chunk* chunk) {
        int baseX = chunk->x * CHUNK_SIZE;
        int baseY = chunk->y * CHUNK_SIZE;
        int baseZ = chunk->z * CHUNK_SIZE;

        for (int bx = 0; bx < CHUNK_SIZE; ++bx) {
            for (int bz = 0; bz < CHUNK_SIZE; ++bz) {
                int worldX = baseX + bx;
                int worldZ = baseZ + bz;

                // 使用 Perlin 噪声生成地形高度
                float height = 8.0f + noise.octaveNoise(worldX * 0.1f, worldZ * 0.1f, 0, 4) * 8.0f;
                
                // 添加山丘变化
                height += noise.octaveNoise(worldX * 0.05f, worldZ * 0.05f, 0, 3) * 4.0f;

                for (int by = 0; by < CHUNK_SIZE; ++by) {
                    int worldY = baseY + by;
                    
                    if (worldY < height - 2) {
                        chunk->setBlock(bx, by, bz, BlockType::STONE);
                    } else if (worldY < height - 1) {
                        chunk->setBlock(bx, by, bz, BlockType::DIRT);
                    } else if (worldY < height) {
                        chunk->setBlock(bx, by, bz, BlockType::GRASS);
                    } else {
                        chunk->setBlock(bx, by, bz, BlockType::AIR);
                    }

                    // 添加树木
                    if (worldY == (int)height && worldX % 12 == 0 && worldZ % 12 == 0) {
                        generateTree(chunk, bx, by, bz);
                    }
                }
            }
        }
    }

    // 生成树木
    void generateTree(Chunk* chunk, int bx, int by, int bz) {
        // 树干
        for (int h = 0; h < 5; ++h) {
            if (by + h < CHUNK_SIZE) {
                chunk->setBlock(bx, by + h, bz, BlockType::WOOD);
            }
        }

        // 树冠
        int treeHeight = by + 4;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dy = 0; dy <= 3; ++dy) {
                    int dist = abs(dx) + abs(dz) + dy;
                    if (dist <= 3 && bx + dx >= 0 && bx + dx < CHUNK_SIZE &&
                        bz + dz >= 0 && bz + dz < CHUNK_SIZE &&
                        treeHeight + dy < CHUNK_SIZE) {
                        chunk->setBlock(bx + dx, treeHeight + dy, bz + dz, BlockType::LEAVES);
                    }
                }
            }
        }
    }

    // 获取世界尺寸
    glm::ivec3 getWorldSize() const {
        return {WORLD_WIDTH * CHUNK_SIZE, WORLD_HEIGHT * CHUNK_SIZE, WORLD_DEPTH * CHUNK_SIZE};
    }

private:
    PerlinNoise noise;
    std::map<std::tuple<int, int, int>, std::unique_ptr<Chunk>> chunks;
};

// =============================================================================
// 玩家类
// =============================================================================

class Player {
public:
    glm::vec3 position = glm::vec3(CHUNK_SIZE * WORLD_WIDTH / 2.0f, 18.0f, CHUNK_SIZE * WORLD_DEPTH / 2.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isOnGround = false;

    // 更新玩家位置
    void update(float dt, const VoxelWorld& world) {
        // 应用重力
        velocity.y -= GRAVITY * dt;

        // 限制最大下落速度
        if (velocity.y < -20.0f) velocity.y = -20.0f;

        // 计算移动方向
        glm::vec3 moveDir(velocity.x, velocity.y, velocity.z);
        moveDir *= dt;

        // 碰撞检测
        glm::vec3 newPos = position + moveDir;
        
        // 分离轴碰撞检测
        if (checkCollision(newPos, world)) {
            // X 方向碰撞
            if (checkCollision(glm::vec3(newPos.x, position.y, position.z), world)) {
                newPos.x = position.x;
                velocity.x = 0;
            }
            // Y 方向碰撞
            if (checkCollision(glm::vec3(position.x, newPos.y, position.z), world)) {
                newPos.y = position.y;
                velocity.y = 0;
                if (moveDir.y < 0) isOnGround = true;
            } else {
                isOnGround = false;
            }
            // Z 方向碰撞
            if (checkCollision(glm::vec3(position.x, position.y, newPos.z), world)) {
                newPos.z = position.z;
                velocity.z = 0;
            }
        }

        position = newPos;

        // 防止玩家掉出世界底部
        if (position.y < 0) {
            position.y = 20.0f;
            velocity.y = 0;
        }
    }

    // 跳跃
    void jump() {
        if (isOnGround) {
            velocity.y = JUMP_FORCE;
            isOnGround = false;
        }
    }

    // 设置水平移动
    void setHorizontalVelocity(const glm::vec3& dir) {
        velocity.x = dir.x * MOVE_SPEED;
        velocity.z = dir.z * MOVE_SPEED;
    }

private:
    // 碰撞检测
    bool checkCollision(const glm::vec3& pos, const VoxelWorld& world) const {
        float halfWidth = PLAYER_SIZE / 2.0f;
        float halfHeight = PLAYER_HEIGHT / 2.0f;

        // 检查玩家碰撞盒内的所有方块
        int startX = floor(pos.x - halfWidth);
        int endX = ceil(pos.x + halfWidth);
        int startY = floor(pos.y - halfHeight);
        int endY = ceil(pos.y + halfHeight);
        int startZ = floor(pos.z - halfWidth);
        int endZ = ceil(pos.z + halfWidth);

        for (int x = startX; x <= endX; ++x) {
            for (int y = startY; y <= endY; ++y) {
                for (int z = startZ; z <= endZ; ++z) {
                    BlockType block = world.getBlock(x, y, z);
                    if (block != BlockType::AIR && block != BlockType::LEAVES) {
                        // AABB 碰撞检测
                        float blockMinX = x;
                        float blockMaxX = x + 1;
                        float blockMinY = y;
                        float blockMaxY = y + 1;
                        float blockMinZ = z;
                        float blockMaxZ = z + 1;

                        float playerMinX = pos.x - halfWidth;
                        float playerMaxX = pos.x + halfWidth;
                        float playerMinY = pos.y - halfHeight;
                        float playerMaxY = pos.y + halfHeight;
                        float playerMinZ = pos.z - halfWidth;
                        float playerMaxZ = pos.z + halfWidth;

                        if (playerMaxX > blockMinX && playerMinX < blockMaxX &&
                            playerMaxY > blockMinY && playerMinY < blockMaxY &&
                            playerMaxZ > blockMinZ && playerMinZ < blockMaxZ) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
};

// =============================================================================
// Vulkan 渲染相关
// =============================================================================

#ifdef ENABLE_VALIDATION
const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
#else
const std::vector<const char*> validationLayers = {};
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    std::cerr << "验证层消息: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

static float g_camYaw = -90.0f;
static float g_camPitch = 0.0f;
static double g_lastX = 400.0, g_lastY = 300.0;
static bool g_firstMouse = true;
static bool g_showMenu = false;
static double g_mouseX = 0.0, g_mouseY = 0.0;

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    g_mouseX = xpos;
    g_mouseY = ypos;
    
    if (g_showMenu) return;
    
    if (g_firstMouse) { g_lastX = xpos; g_lastY = ypos; g_firstMouse = false; }
    double xoffset = xpos - g_lastX;
    double yoffset = g_lastY - ypos;
    g_lastX = xpos;
    g_lastY = ypos;
    const float sensitivity = 0.1f;
    g_camYaw += (float)xoffset * sensitivity;
    g_camPitch += (float)yoffset * sensitivity;
    if (g_camPitch > 89.0f) g_camPitch = 89.0f;
    if (g_camPitch < -89.0f) g_camPitch = -89.0f;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, messenger, pAllocator);
    }
}

// 生成体素网格顶点数据
void generateVoxelMesh(const VoxelWorld& world, std::vector<float>& vertices) {
    vertices.clear();
    glm::ivec3 worldSize = world.getWorldSize();

    // 遍历所有方块
    for (int x = 0; x < worldSize.x; ++x) {
        for (int y = 0; y < worldSize.y; ++y) {
            for (int z = 0; z < worldSize.z; ++z) {
                BlockType block = world.getBlock(x, y, z);
                if (block == BlockType::AIR) continue;

                glm::vec3 color = BLOCK_COLORS[(int)block];

                // 检查六个面是否需要渲染
                bool front = world.getBlock(x, y, z + 1) == BlockType::AIR;
                bool back = world.getBlock(x, y, z - 1) == BlockType::AIR;
                bool left = world.getBlock(x - 1, y, z) == BlockType::AIR;
                bool right = world.getBlock(x + 1, y, z) == BlockType::AIR;
                bool top = world.getBlock(x, y + 1, z) == BlockType::AIR;
                bool bottom = world.getBlock(x, y - 1, z) == BlockType::AIR;

                float px = x * BLOCK_SIZE;
                float py = y * BLOCK_SIZE;
                float pz = z * BLOCK_SIZE;

                // 前面
                if (front) {
                    vertices.insert(vertices.end(), {
                        px, py, pz + BLOCK_SIZE, 0,0,1, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py, pz + BLOCK_SIZE, 0,0,1, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz + BLOCK_SIZE, 0,0,1, color.r, color.g, color.b,
                        px, py, pz + BLOCK_SIZE, 0,0,1, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz + BLOCK_SIZE, 0,0,1, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz + BLOCK_SIZE, 0,0,1, color.r, color.g, color.b
                    });
                }
                // 后面
                if (back) {
                    vertices.insert(vertices.end(), {
                        px, py, pz, 0,0,-1, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz, 0,0,-1, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz, 0,0,-1, color.r, color.g, color.b,
                        px, py, pz, 0,0,-1, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz, 0,0,-1, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py, pz, 0,0,-1, color.r, color.g, color.b
                    });
                }
                // 左面
                if (left) {
                    vertices.insert(vertices.end(), {
                        px, py, pz, -1,0,0, color.r, color.g, color.b,
                        px, py, pz + BLOCK_SIZE, -1,0,0, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz + BLOCK_SIZE, -1,0,0, color.r, color.g, color.b,
                        px, py, pz, -1,0,0, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz + BLOCK_SIZE, -1,0,0, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz, -1,0,0, color.r, color.g, color.b
                    });
                }
                // 右面
                if (right) {
                    vertices.insert(vertices.end(), {
                        px + BLOCK_SIZE, py, pz, 1,0,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz, 1,0,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz + BLOCK_SIZE, 1,0,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py, pz, 1,0,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz + BLOCK_SIZE, 1,0,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py, pz + BLOCK_SIZE, 1,0,0, color.r, color.g, color.b
                    });
                }
                // 顶面
                if (top) {
                    vertices.insert(vertices.end(), {
                        px, py + BLOCK_SIZE, pz, 0,1,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz, 0,1,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz + BLOCK_SIZE, 0,1,0, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz, 0,1,0, color.r, color.g, color.b,
                        px + BLOCK_SIZE, py + BLOCK_SIZE, pz + BLOCK_SIZE, 0,1,0, color.r, color.g, color.b,
                        px, py + BLOCK_SIZE, pz + BLOCK_SIZE, 0,1,0, color.r, color.g, color.b
                    });
                }
                // 底面
                if (bottom) {
                    vertices.insert(vertices.end(), {
                        px, py, pz, 0,-1,0, color.r*0.7f, color.g*0.7f, color.b*0.7f,
                        px, py, pz + BLOCK_SIZE, 0,-1,0, color.r*0.7f, color.g*0.7f, color.b*0.7f,
                        px + BLOCK_SIZE, py, pz + BLOCK_SIZE, 0,-1,0, color.r*0.7f, color.g*0.7f, color.b*0.7f,
                        px, py, pz, 0,-1,0, color.r*0.7f, color.g*0.7f, color.b*0.7f,
                        px + BLOCK_SIZE, py, pz + BLOCK_SIZE, 0,-1,0, color.r*0.7f, color.g*0.7f, color.b*0.7f,
                        px + BLOCK_SIZE, py, pz, 0,-1,0, color.r*0.7f, color.g*0.7f, color.b*0.7f
                    });
                }
            }
        }
    }
}

// =============================================================================
// 主函数
// =============================================================================

int main() {
    std::freopen("voxel.stdout.txt","w",stdout);
    std::freopen("voxel.stderr.txt","w",stderr);

    try {
        // 初始化 FreeType
        if (FT_Init_FreeType(&g_ftLibrary)) {
            std::cerr << "FreeType 初始化失败" << std::endl;
        } else {
            // 尝试加载系统字体
            const char* fontPaths[] = {
                "C:/Windows/Fonts/simsun.ttc",
                "C:/Windows/Fonts/msyh.ttc",
                "C:/Windows/Fonts/arial.ttf",
                "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"
            };
            bool fontLoaded = false;
            for (const char* path : fontPaths) {
                if (FT_New_Face(g_ftLibrary, path, 0, &g_ftFace) == 0) {
                    FT_Set_Pixel_Sizes(g_ftFace, 0, 48);
                    fontLoaded = true;
                    std::cerr << "字体加载成功: " << path << std::endl;
                    break;
                }
            }
            if (!fontLoaded) {
                std::cerr << "无法加载字体" << std::endl;
            }
        }

        // 初始化 GLFW
        if (!glfwInit()) throw std::runtime_error("GLFW 初始化失败");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(1280, 720, "StartingFromNothing", nullptr, nullptr);
        if (!window) throw std::runtime_error("窗口创建失败");

        // 创建 VoxelWorld
        VoxelWorld world;
        Player player;

        // Vulkan 初始化（简化版）
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Voxel Sandbox";
        appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1,0,0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifdef ENABLE_VALIDATION
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
#else
        createInfo.enabledLayerCount = 0;
#endif
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("实例创建失败");

        // 创建表面
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("表面创建失败");

        // 选择物理设备
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) throw std::runtime_error("未找到 GPU");
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        VkPhysicalDevice physical = devices[0];
        
        std::cerr << "物理设备选择成功" << std::endl << std::flush;

        // 查找队列族
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, qprops.data());

        int graphicsFamily = -1, presentFamily = -1;
        for (int i = 0; i < (int)qprops.size(); ++i) {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily = i;
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
            if (present) presentFamily = i;
        }

        if (graphicsFamily < 0 || presentFamily < 0) throw std::runtime_error("未找到合适的队列族");

        // 创建逻辑设备
        float priority = 1.0f;
        std::set<int> families = {graphicsFamily, presentFamily};
        std::vector<VkDeviceQueueCreateInfo> qcis;
        for (int f : families) {
            VkDeviceQueueCreateInfo qci{};
            qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = f;
            qci.queueCount = 1;
            qci.pQueuePriorities = &priority;
            qcis.push_back(qci);
        }

        VkDevice device;
        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = (uint32_t)qcis.size();
        dci.pQueueCreateInfos = qcis.data();
        const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = devExts;

        if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("逻辑设备创建失败");

        std::cerr << "逻辑设备创建成功" << std::endl << std::flush;

        VkQueue graphicsQueue;
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        VkQueue presentQueue;
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);

        std::cerr << "队列获取成功" << std::endl << std::flush;

        // 生成网格数据
        std::vector<float> vertices;
        
        // 添加一个测试方块（确保能看到东西）
        world.setBlock(30, 15, 30, BlockType::GRASS);
        world.setBlock(31, 15, 30, BlockType::GRASS);
        world.setBlock(30, 16, 30, BlockType::GRASS);
        
        std::cerr << "开始生成体素网格..." << std::endl << std::flush;
        generateVoxelMesh(world, vertices);
        std::cerr << "网格生成完成，顶点数: " << vertices.size() / 9 << std::endl << std::flush;
        
        // 如果网格为空，使用测试三角形
        if (vertices.empty()) {
            std::cerr << "网格为空，使用测试三角形" << std::endl << std::flush;
            vertices = {
                32.0f,  30.0f,  20.0f,  0,0,1,  1,0,0,
                27.0f,  20.0f,  20.0f,  0,0,1,  0,1,0,
                37.0f,  20.0f,  20.0f,  0,0,1,  0,0,1
            };
        }

        // 创建顶点缓冲区
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

        VkDeviceSize bufferSize = vertices.size() * sizeof(float);
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = bufferSize;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bci, nullptr, &vertexBuffer) != VK_SUCCESS)
            throw std::runtime_error("顶点缓冲区创建失败");

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, vertexBuffer, &memReq);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
        int memoryTypeIndex = -1;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                memoryTypeIndex = (int)i;
                break;
            }
        }

        if (memoryTypeIndex == -1) throw std::runtime_error("未找到内存类型");
        mai.memoryTypeIndex = memoryTypeIndex;

        if (vkAllocateMemory(device, &mai, nullptr, &vertexBufferMemory) != VK_SUCCESS)
            throw std::runtime_error("内存分配失败");

        vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);
        void* data;
        vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, vertexBufferMemory);

        // 创建 Swapchain 和渲染资源
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent{1280, 720};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<VkFramebuffer> swapchainFramebuffers;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkSemaphore> renderFinishedSemaphores;

        auto createSwapchainAndResources = [&](bool first) -> void {
            if (!first) {
                vkDeviceWaitIdle(device);
                for (auto fb : swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
                swapchainFramebuffers.clear();
                for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
                swapchainImageViews.clear();
                if (graphicsPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, graphicsPipeline, nullptr); graphicsPipeline = VK_NULL_HANDLE; }
                if (pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
                if (renderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE; }
                if (commandPool != VK_NULL_HANDLE) { vkDestroyCommandPool(device, commandPool, nullptr); commandPool = VK_NULL_HANDLE; }
                if (swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
            }

            VkSurfaceCapabilitiesKHR caps;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
            swapchainExtent = {1280, 720};

            VkSurfaceFormatKHR surfaceFormat{};
            surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
            surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

            uint32_t imageCount = caps.minImageCount + 1;
            if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

            VkSwapchainCreateInfoKHR sci{};
            sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            sci.surface = surface;
            sci.minImageCount = imageCount;
            sci.imageFormat = surfaceFormat.format;
            sci.imageColorSpace = surfaceFormat.colorSpace;
            sci.imageExtent = swapchainExtent;
            sci.imageArrayLayers = 1;
            sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

            uint32_t qfIndices[] = {(uint32_t)graphicsFamily, (uint32_t)presentFamily};
            if (graphicsFamily != presentFamily) {
                sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                sci.queueFamilyIndexCount = 2;
                sci.pQueueFamilyIndices = qfIndices;
            } else {
                sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            }

            sci.preTransform = caps.currentTransform;
            sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            sci.clipped = VK_TRUE;
            sci.oldSwapchain = VK_NULL_HANDLE;

            if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS)
                throw std::runtime_error("Swapchain 创建失败");

            uint32_t scImageCount = 0;
            vkGetSwapchainImagesKHR(device, swapchain, &scImageCount, nullptr);
            swapchainImages.resize(scImageCount);
            vkGetSwapchainImagesKHR(device, swapchain, &scImageCount, swapchainImages.data());

            swapchainImageViews.resize(scImageCount);
            for (uint32_t i = 0; i < scImageCount; ++i) {
                VkImageViewCreateInfo iv{};
                iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                iv.image = swapchainImages[i];
                iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
                iv.format = surfaceFormat.format;
                iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                iv.subresourceRange.baseMipLevel = 0;
                iv.subresourceRange.levelCount = 1;
                iv.subresourceRange.baseArrayLayer = 0;
                iv.subresourceRange.layerCount = 1;
                if (vkCreateImageView(device, &iv, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
                    throw std::runtime_error("图像视图创建失败");
            }

            // Render Pass with depth
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = surfaceFormat.format;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = VK_FORMAT_D32_SFLOAT;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthRef{};
            depthRef.attachment = 1;
            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            subpass.pDepthStencilAttachment = &depthRef;

            VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
            VkRenderPassCreateInfo rpci{};
            rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpci.attachmentCount = 2;
            rpci.pAttachments = attachments;
            rpci.subpassCount = 1;
            rpci.pSubpasses = &subpass;

            if (vkCreateRenderPass(device, &rpci, nullptr, &renderPass) != VK_SUCCESS)
                throw std::runtime_error("渲染通道创建失败");

            // Pipeline
            VkShaderModuleCreateInfo smci{};
            smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

            smci.codeSize = vert_spv_size;
            smci.pCode = (const uint32_t*)vert_spv;
            VkShaderModule vertSm;
            if (vkCreateShaderModule(device, &smci, nullptr, &vertSm) != VK_SUCCESS)
                throw std::runtime_error("顶点着色器创建失败");

            smci.codeSize = frag_spv_size;
            smci.pCode = (const uint32_t*)frag_spv;
            VkShaderModule fragSm;
            if (vkCreateShaderModule(device, &smci, nullptr, &fragSm) != VK_SUCCESS)
                throw std::runtime_error("片段着色器创建失败");

            VkPipelineShaderStageCreateInfo vertStage{};
            vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertStage.module = vertSm;
            vertStage.pName = "main";

            VkPipelineShaderStageCreateInfo fragStage{};
            fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStage.module = fragSm;
            fragStage.pName = "main";

            VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

            // Vertex input: pos(3), normal(3), color(3)
            VkVertexInputBindingDescription bindingDesc{};
            bindingDesc.binding = 0;
            bindingDesc.stride = 9 * sizeof(float);
            bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attrDescs[3]{};
            attrDescs[0].location = 0;
            attrDescs[0].binding = 0;
            attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[0].offset = 0;

            attrDescs[1].location = 1;
            attrDescs[1].binding = 0;
            attrDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[1].offset = 3 * sizeof(float);

            attrDescs[2].location = 2;
            attrDescs[2].binding = 0;
            attrDescs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[2].offset = 6 * sizeof(float);

            VkPipelineVertexInputStateCreateInfo vp{};
            vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vp.vertexBindingDescriptionCount = 1;
            vp.pVertexBindingDescriptions = &bindingDesc;
            vp.vertexAttributeDescriptionCount = 3;
            vp.pVertexAttributeDescriptions = attrDescs;

            VkPipelineInputAssemblyStateCreateInfo ia{};
            ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            ia.primitiveRestartEnable = VK_FALSE;

            VkViewport viewport{};
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = (float)swapchainExtent.width;
            viewport.height = (float)swapchainExtent.height;
            viewport.minDepth = 0;
            viewport.maxDepth = 1;

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = swapchainExtent;

            VkPipelineViewportStateCreateInfo vpstate{};
            vpstate.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vpstate.viewportCount = 1;
            vpstate.pViewports = &viewport;
            vpstate.scissorCount = 1;
            vpstate.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rast{};
            rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rast.depthClampEnable = VK_FALSE;
            rast.rasterizerDiscardEnable = VK_FALSE;
            rast.polygonMode = VK_POLYGON_MODE_FILL;
            rast.lineWidth = 1.0f;
            rast.cullMode = VK_CULL_MODE_NONE;  // 禁用背面剔除
            rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rast.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.sampleShadingEnable = VK_FALSE;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            // Depth stencil state
            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencil.depthBoundsTestEnable = VK_FALSE;
            depthStencil.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState cbatt{};
            cbatt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cbatt.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.logicOpEnable = VK_FALSE;
            cb.attachmentCount = 1;
            cb.pAttachments = &cbatt;

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pcr.offset = 0;
            pcr.size = sizeof(float) * 16;

            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.pushConstantRangeCount = 1;
            plci.pPushConstantRanges = &pcr;

            if (vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
                throw std::runtime_error("管线布局创建失败");

            VkGraphicsPipelineCreateInfo gpci{};
            gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gpci.stageCount = 2;
            gpci.pStages = stages;
            gpci.pVertexInputState = &vp;
            gpci.pInputAssemblyState = &ia;
            gpci.pViewportState = &vpstate;
            gpci.pRasterizationState = &rast;
            gpci.pMultisampleState = &ms;
            gpci.pDepthStencilState = &depthStencil;
            gpci.pColorBlendState = &cb;
            gpci.layout = pipelineLayout;
            gpci.renderPass = renderPass;
            gpci.subpass = 0;

            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &graphicsPipeline) != VK_SUCCESS)
                throw std::runtime_error("图形管线创建失败");

            vkDestroyShaderModule(device, fragSm, nullptr);
            vkDestroyShaderModule(device, vertSm, nullptr);

            // Depth image
            VkImage depthImage;
            VkDeviceMemory depthImageMemory;
            
            VkImageCreateInfo dci{};
            dci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            dci.imageType = VK_IMAGE_TYPE_2D;
            dci.extent.width = swapchainExtent.width;
            dci.extent.height = swapchainExtent.height;
            dci.extent.depth = 1;
            dci.mipLevels = 1;
            dci.arrayLayers = 1;
            dci.format = VK_FORMAT_D32_SFLOAT;
            dci.tiling = VK_IMAGE_TILING_OPTIMAL;
            dci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            dci.samples = VK_SAMPLE_COUNT_1_BIT;
            dci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            
            if (vkCreateImage(device, &dci, nullptr, &depthImage) != VK_SUCCESS)
                throw std::runtime_error("深度图像创建失败");
            
            VkMemoryRequirements dmemReq;
            vkGetImageMemoryRequirements(device, depthImage, &dmemReq);
            VkMemoryAllocateInfo dmai{};
            dmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            dmai.allocationSize = dmemReq.size;
            
            int dmemTypeIndex = -1;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((dmemReq.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                    dmemTypeIndex = (int)i;
                    break;
                }
            }
            if (dmemTypeIndex == -1) throw std::runtime_error("未找到深度图像内存类型");
            dmai.memoryTypeIndex = dmemTypeIndex;
            
            if (vkAllocateMemory(device, &dmai, nullptr, &depthImageMemory) != VK_SUCCESS)
                throw std::runtime_error("深度图像内存分配失败");
            vkBindImageMemory(device, depthImage, depthImageMemory, 0);
            
            // Depth image view
            VkImageView depthImageView;
            VkImageViewCreateInfo div{};
            div.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            div.image = depthImage;
            div.viewType = VK_IMAGE_VIEW_TYPE_2D;
            div.format = VK_FORMAT_D32_SFLOAT;
            div.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            div.subresourceRange.baseMipLevel = 0;
            div.subresourceRange.levelCount = 1;
            div.subresourceRange.baseArrayLayer = 0;
            div.subresourceRange.layerCount = 1;
            
            if (vkCreateImageView(device, &div, nullptr, &depthImageView) != VK_SUCCESS)
                throw std::runtime_error("深度图像视图创建失败");
            
            // Framebuffers
            swapchainFramebuffers.resize(swapchainImageViews.size());
            for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
                VkImageView avs[] = {swapchainImageViews[i], depthImageView};
                VkFramebufferCreateInfo fbci{};
                fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbci.renderPass = renderPass;
                fbci.attachmentCount = 2;
                fbci.pAttachments = avs;
                fbci.width = swapchainExtent.width;
                fbci.height = swapchainExtent.height;
                fbci.layers = 1;
                if (vkCreateFramebuffer(device, &fbci, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS)
                    throw std::runtime_error("帧缓冲区创建失败");
            }

            // Command Pool
            VkCommandPoolCreateInfo cpci{};
            cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cpci.queueFamilyIndex = graphicsFamily;
            cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            if (vkCreateCommandPool(device, &cpci, nullptr, &commandPool) != VK_SUCCESS)
                throw std::runtime_error("命令池创建失败");

            // Command Buffers
            commandBuffers.resize(swapchainFramebuffers.size());
            VkCommandBufferAllocateInfo cbai{};
            cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbai.commandPool = commandPool;
            cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbai.commandBufferCount = (uint32_t)commandBuffers.size();

            if (vkAllocateCommandBuffers(device, &cbai, commandBuffers.data()) != VK_SUCCESS)
                throw std::runtime_error("命令缓冲区分配失败");

            // Semaphores
            if (!renderFinishedSemaphores.empty()) {
                for (auto s : renderFinishedSemaphores) vkDestroySemaphore(device, s, nullptr);
                renderFinishedSemaphores.clear();
            }
            renderFinishedSemaphores.resize(commandBuffers.size());
            VkSemaphoreCreateInfo semci{};
            semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            for (size_t ri = 0; ri < renderFinishedSemaphores.size(); ++ri) {
                if (vkCreateSemaphore(device, &semci, nullptr, &renderFinishedSemaphores[ri]) != VK_SUCCESS)
                    throw std::runtime_error("信号量创建失败");
            }
        };

        createSwapchainAndResources(true);

        // 同步对象
        const int MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkFence> inFlightFences(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkFence> imagesInFlight;

        VkSemaphoreCreateInfo semci{};
        semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateSemaphore(device, &semci, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS)
                throw std::runtime_error("信号量创建失败");
            if (vkCreateFence(device, &fci, nullptr, &inFlightFences[i]) != VK_SUCCESS)
                throw std::runtime_error("栅栏创建失败");
        }

        imagesInFlight.resize(commandBuffers.size(), VK_NULL_HANDLE);

        // 输入设置
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPosCallback(window, cursor_pos_callback);

        double lastTime = glfwGetTime();
        size_t currentFrame = 0;

        // 主循环
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // ⭐ 关键：每次循环开头再次检查窗口是否关闭
            if (glfwWindowShouldClose(window)) break;

            // 计算 delta time
            double now = glfwGetTime();
            float dt = float(now - lastTime);
            lastTime = now;

            // 处理ESC键切换菜单
            static bool escapePressed = false;
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                if (!escapePressed) {
                    g_showMenu = !g_showMenu;
                    escapePressed = true;
                    std::cerr << "菜单状态: " << (g_showMenu ? "显示" : "隐藏") << std::endl << std::flush;
                    
                    // 如果是从菜单返回游戏，需要恢复顶点缓冲区
                    if (!g_showMenu) {
                        std::cerr << "恢复体素网格..." << std::endl << std::flush;
                        std::vector<float> gameVertices;
                        generateVoxelMesh(world, gameVertices);
                        
                        void* data;
                        vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
                        size_t copySize = std::min(gameVertices.size() * sizeof(float), bufferSize);
                        memcpy(data, gameVertices.data(), copySize);
                        vkUnmapMemory(device, vertexBufferMemory);
                        
                        vertices = gameVertices;
                        std::cerr << "网格恢复完成，顶点数: " << vertices.size() / 9 << std::endl << std::flush;
                    }
                    
                    if (g_showMenu) {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    } else {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        g_firstMouse = true;
                    }
                }
            } else {
                escapePressed = false; // 重置标志，允许下次按键
            }
            
            // 如果显示菜单，跳过游戏逻辑
            if (g_showMenu) {
                // 检查鼠标点击退出按钮（只在鼠标按下时检测一次）
                static bool mousePressed = false;
                if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                    if (!mousePressed) {
                        mousePressed = true;
                        int width, height;
                        glfwGetWindowSize(window, &width, &height);
                        // 按钮区域：屏幕中央，宽200px，高50px
                        float btnX = (width - 200) / 2.0f;
                        float btnY = (height - 50) / 2.0f;
                        if (g_mouseX >= btnX && g_mouseX <= btnX + 200 &&
                            g_mouseY >= btnY && g_mouseY <= btnY + 50) {
                            glfwSetWindowShouldClose(window, GLFW_TRUE);
                        }
                    }
                } else {
                    mousePressed = false;
                }
                
                // ⭐ 关键：检查窗口是否关闭
                if (glfwWindowShouldClose(window)) break;
                
                // 使用游戏主循环的渲染流程，只显示背景
                // 等待栅栏
                vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
                
                // 获取图像
                uint32_t imageIndex;
                VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                      imageAvailableSemaphores[currentFrame],
                                                      VK_NULL_HANDLE, &imageIndex);
                
                if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
                    createSwapchainAndResources(false);
                    imagesInFlight.assign(commandBuffers.size(), VK_NULL_HANDLE);
                    continue;
                } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
                    throw std::runtime_error("获取图像失败");
                }
                
                if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
                    vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
                }
                imagesInFlight[imageIndex] = inFlightFences[currentFrame];
                
                vkResetFences(device, 1, &inFlightFences[currentFrame]);
                
                // 录制命令缓冲区
                VkCommandBufferBeginInfo binfo{};
                binfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                binfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                
                if (vkBeginCommandBuffer(commandBuffers[imageIndex], &binfo) != VK_SUCCESS)
                    throw std::runtime_error("命令缓冲区开始录制失败");
                
                VkRenderPassBeginInfo rpbi{};
                rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpbi.renderPass = renderPass;
                rpbi.framebuffer = swapchainFramebuffers[imageIndex];
                rpbi.renderArea.offset = {0, 0};
                rpbi.renderArea.extent = swapchainExtent;
                VkClearValue clearVals[2];
                clearVals[0].color.float32[0] = 0.1f;
                clearVals[0].color.float32[1] = 0.15f;
                clearVals[0].color.float32[2] = 0.25f;
                clearVals[0].color.float32[3] = 1.0f;
                clearVals[1].depthStencil.depth = 1.0f;
                clearVals[1].depthStencil.stencil = 0;
                rpbi.clearValueCount = 2;
                rpbi.pClearValues = clearVals;
                
                vkCmdBeginRenderPass(commandBuffers[imageIndex], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                
                // 渲染菜单按钮 - 使用一个简单的红色方块
                vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
                
                // 先渲染按钮背景（红色方块）
                std::vector<float> btnVertices = {
                    -0.3f,  0.15f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f,
                     0.3f,  0.15f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f,
                     0.3f, -0.15f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f,
                    -0.3f,  0.15f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f,
                     0.3f, -0.15f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f,
                    -0.3f, -0.15f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f
                };
                
                // 更新顶点缓冲区内容
                void* data;
                vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
                size_t copySize = std::min(btnVertices.size() * sizeof(float), bufferSize);
                memcpy(data, btnVertices.data(), copySize);
                vkUnmapMemory(device, vertexBufferMemory);
                
                VkDeviceSize btnOffsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, &vertexBuffer, btnOffsets);
                
                // 使用恒等矩阵（按钮已经在NDC坐标中）
                glm::mat4 mvp = glm::mat4(1.0f);
                vkCmdPushConstants(commandBuffers[imageIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
                vkCmdDraw(commandBuffers[imageIndex], static_cast<uint32_t>(btnVertices.size() / 9), 1, 0, 0);
                
                // 再渲染文字（使用FreeType）
                std::vector<float> textVertices;
                renderTextToVertices("EXIT", -0.15f, 0.03f, 0.02f, textVertices);
                
                // 更新顶点缓冲区内容
                vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
                copySize = std::min(textVertices.size() * sizeof(float), bufferSize);
                memcpy(data, textVertices.data(), copySize);
                vkUnmapMemory(device, vertexBufferMemory);
                
                VkDeviceSize textOffsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, &vertexBuffer, textOffsets);
                
                vkCmdPushConstants(commandBuffers[imageIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
                vkCmdDraw(commandBuffers[imageIndex], static_cast<uint32_t>(textVertices.size() / 9), 1, 0, 0);
                
                vkCmdEndRenderPass(commandBuffers[imageIndex]);
                
                if (vkEndCommandBuffer(commandBuffers[imageIndex]) != VK_SUCCESS)
                    throw std::runtime_error("命令缓冲区结束录制失败");
                
                // 提交
                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                VkSemaphore waits[] = {imageAvailableSemaphores[currentFrame]};
                VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
                submit.waitSemaphoreCount = 1;
                submit.pWaitSemaphores = waits;
                submit.pWaitDstStageMask = waitStages;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &commandBuffers[imageIndex];
                VkSemaphore signals[] = {renderFinishedSemaphores[imageIndex]};
                submit.signalSemaphoreCount = 1;
                submit.pSignalSemaphores = signals;
                
                if (vkQueueSubmit(graphicsQueue, 1, &submit, inFlightFences[currentFrame]) != VK_SUCCESS)
                    throw std::runtime_error("队列提交失败");
                
                // 呈现
                VkPresentInfoKHR pres{};
                pres.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                pres.waitSemaphoreCount = 1;
                pres.pWaitSemaphores = signals;
                pres.swapchainCount = 1;
                pres.pSwapchains = &swapchain;
                pres.pImageIndices = &imageIndex;
                
                VkResult presRes = vkQueuePresentKHR(presentQueue, &pres);
                if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR) {
                    createSwapchainAndResources(false);
                    imagesInFlight.assign(commandBuffers.size(), VK_NULL_HANDLE);
                }
                
                currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
                continue;
            }
            
            // 处理输入
            glm::vec3 moveDir(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir.z += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir.z -= 1.0f;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir.x -= 1.0f;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir.x += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) player.jump();

            // 将移动方向转换为相机坐标系
            float yawRad = glm::radians(g_camYaw);
            glm::vec3 forward(cos(yawRad), 0, sin(yawRad));
            glm::vec3 right(-sin(yawRad), 0, cos(yawRad));

            glm::vec3 cameraMoveDir = moveDir.x * right + moveDir.z * forward;
            if (glm::length(cameraMoveDir) > 0.001f) {
                cameraMoveDir = glm::normalize(cameraMoveDir);
            } else {
                cameraMoveDir = glm::vec3(0.0f);
            }
            player.setHorizontalVelocity(cameraMoveDir);

            // 更新玩家
            player.update(dt, world);

            // 等待栅栏
            vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

            // 获取图像
            uint32_t imageIndex;
            VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                  imageAvailableSemaphores[currentFrame],
                                                  VK_NULL_HANDLE, &imageIndex);

            if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
                createSwapchainAndResources(false);
                imagesInFlight.assign(commandBuffers.size(), VK_NULL_HANDLE);
                continue;
            } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
                throw std::runtime_error("获取图像失败");
            }

            if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
            }
            imagesInFlight[imageIndex] = inFlightFences[currentFrame];

            vkResetFences(device, 1, &inFlightFences[currentFrame]);

            // 录制命令缓冲区
            vkResetCommandPool(device, commandPool, 0);
            VkCommandBufferBeginInfo binfo{};
            binfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            binfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (vkBeginCommandBuffer(commandBuffers[imageIndex], &binfo) != VK_SUCCESS)
                throw std::runtime_error("命令缓冲区开始录制失败");

            VkRenderPassBeginInfo rpbi{};
            rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpbi.renderPass = renderPass;
            rpbi.framebuffer = swapchainFramebuffers[imageIndex];
            rpbi.renderArea.offset = {0, 0};
            rpbi.renderArea.extent = swapchainExtent;
            VkClearValue clearVals[2];
            clearVals[0].color.float32[0] = 0.1f;
            clearVals[0].color.float32[1] = 0.15f;
            clearVals[0].color.float32[2] = 0.25f;
            clearVals[0].color.float32[3] = 1.0f;
            clearVals[1].depthStencil.depth = 1.0f;
            clearVals[1].depthStencil.stencil = 0;
            rpbi.clearValueCount = 2;
            rpbi.pClearValues = clearVals;

            vkCmdBeginRenderPass(commandBuffers[imageIndex], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            VkBuffer vbs[] = {vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, vbs, offsets);

            // 计算相机矩阵（玩家位置作为相机位置）
            float aspect = (float)swapchainExtent.width / (float)swapchainExtent.height;
            glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 200.0f);
            proj[1][1] *= -1;

            float currentYawRad = glm::radians(g_camYaw);
            float currentPitchRad = glm::radians(g_camPitch);
            glm::vec3 lookDir(cos(currentPitchRad) * cos(currentYawRad), sin(currentPitchRad), cos(currentPitchRad) * sin(currentYawRad));
            glm::mat4 view = glm::lookAt(player.position + glm::vec3(0, PLAYER_HEIGHT - 0.2f, 0),
                                         player.position + glm::vec3(0, PLAYER_HEIGHT - 0.2f, 0) + lookDir,
                                         glm::vec3(0, 1, 0));

            // 调试输出
            static int frameCount = 0;
            if (frameCount == 0) {
                std::cerr << "玩家位置: (" << player.position.x << ", " << player.position.y << ", " << player.position.z << ")" << std::endl << std::flush;
                std::cerr << "相机朝向: yaw=" << g_camYaw << ", pitch=" << g_camPitch << std::endl << std::flush;
                std::cerr << "观察方向: (" << lookDir.x << ", " << lookDir.y << ", " << lookDir.z << ")" << std::endl << std::flush;
            }
            frameCount++;

            glm::mat4 model = glm::mat4(1.0f);
            glm::mat4 mvp = proj * view * model;

            vkCmdPushConstants(commandBuffers[imageIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
            vkCmdDraw(commandBuffers[imageIndex], (uint32_t)(vertices.size() / 9), 1, 0, 0);

            vkCmdEndRenderPass(commandBuffers[imageIndex]);

            if (vkEndCommandBuffer(commandBuffers[imageIndex]) != VK_SUCCESS)
                throw std::runtime_error("命令缓冲区结束录制失败");

            // 提交
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VkSemaphore waits[] = {imageAvailableSemaphores[currentFrame]};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = waits;
            submit.pWaitDstStageMask = waitStages;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &commandBuffers[imageIndex];
            VkSemaphore signals[] = {renderFinishedSemaphores[imageIndex]};
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = signals;

            if (vkQueueSubmit(graphicsQueue, 1, &submit, inFlightFences[currentFrame]) != VK_SUCCESS)
                throw std::runtime_error("队列提交失败");

            // 呈现
            VkPresentInfoKHR pres{};
            pres.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pres.waitSemaphoreCount = 1;
            pres.pWaitSemaphores = signals;
            pres.swapchainCount = 1;
            pres.pSwapchains = &swapchain;
            pres.pImageIndices = &imageIndex;

            VkResult presRes = vkQueuePresentKHR(presentQueue, &pres);
            if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR) {
                createSwapchainAndResources(false);
                imagesInFlight.assign(commandBuffers.size(), VK_NULL_HANDLE);
            }

            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        // 清理
        vkDeviceWaitIdle(device);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkDestroyFence(device, inFlightFences[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        }
        for (auto s : renderFinishedSemaphores) vkDestroySemaphore(device, s, nullptr);
        for (auto fb : swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
        if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
        if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, vertexBuffer, nullptr);
        if (vertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(device, vertexBufferMemory, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
#ifdef ENABLE_VALIDATION
        if (debugMessenger != VK_NULL_HANDLE) DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif
        vkDestroyInstance(instance, nullptr);
        // 清理 FreeType（在GLFW终止之前）
        if (g_ftFace != nullptr) FT_Done_Face(g_ftFace);
        if (g_ftLibrary != nullptr) FT_Done_FreeType(g_ftLibrary);

        glfwDestroyWindow(window);
        glfwTerminate();

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
