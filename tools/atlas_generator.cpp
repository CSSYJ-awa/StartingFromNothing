/**
 * atlas_generator.cpp —— 纹理图集生成工具
 *
 * 程序化生成所有方块类型的纹理并拼装为图集。
 * 输出 PNG 文件，可使用 BC7E 或 NVIDIA Texture Tools 压缩为 BC7。
 *
 * 编译: g++ atlas_generator.cpp -o atlas_generator -lstb_image_write
 * 运行: ./atlas_generator
 * 压缩: bc7e -f png -o atlas.ktx atlas.png
 */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

// 简易 PNG 写入（不依赖 stb_image_write 的独立实现）
// 实际使用建议链接 stb_image_write.h
static void writePNG(const char* path, int w, int h, const uint8_t* data)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "无法写入 %s\n", path); return; }

    // PNG 签名
    const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, fp);

    // IHDR 块（简化实现：实际应使用 zlib 压缩）
    // ... 此处省略完整 PNG 编码，实际使用建议链接 stb_image_write
    fprintf(stderr, "提示：请链接 stb_image_write.h 获得完整 PNG 输出\n");

    fclose(fp);
}

// ============================================================================
// 程序化纹理生成
// ============================================================================

/** 生成一个方块的纹理（16×16 RGBA） */
void generateBlockTexture(uint8_t* texel, int tileSize,
                          uint8_t r, uint8_t g, uint8_t b,
                          float roughness = 0.3f, float detail = 0.5f)
{
    for (int y = 0; y < tileSize; ++y)
    {
        for (int x = 0; x < tileSize; ++x)
        {
            int idx = (y * tileSize + x) * 4;

            // 基础颜色
            float fr = r, fg = g, fb = b;

            // 简单噪点（模拟纹理细节）
            float noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * detail * 30.0f;

            // 边缘加深（模拟轮廓）
            float edgeX = std::min(x, tileSize - 1 - x) / static_cast<float>(tileSize);
            float edgeY = std::min(y, tileSize - 1 - y) / static_cast<float>(tileSize);
            float edgeFactor = 1.0f - 0.3f * (1.0f - std::min(edgeX, edgeY));

            texel[idx + 0] = static_cast<uint8_t>(std::clamp(fr + noise, 0.0f, 255.0f) * edgeFactor / 255.0f * 255.0f);
            texel[idx + 1] = static_cast<uint8_t>(std::clamp(fg + noise, 0.0f, 255.0f) * edgeFactor / 255.0f * 255.0f);
            texel[idx + 2] = static_cast<uint8_t>(std::clamp(fb + noise, 0.0f, 255.0f) * edgeFactor / 255.0f * 255.0f);
            texel[idx + 3] = 255; // Alpha
        }
    }
}

int main()
{
    constexpr int TILE_SIZE = 16;
    constexpr int TILES_X   = 16;
    constexpr int TILES_Y   = 4;
    constexpr int ATLAS_W   = TILE_SIZE * TILES_X;
    constexpr int ATLAS_H   = TILE_SIZE * TILES_Y;

    // 分配图集内存
    std::vector<uint8_t> atlas(ATLAS_W * ATLAS_H * 4, 0);

    // 方块纹理定义 (R, G, B, name)
    struct BlockDef { uint8_t r, g, b; float roughness; const char* name; };
    const BlockDef blocks[] = {
        {0x44, 0x9E, 0x44, 0.4f, "Grass_Top"},    // 草顶
        {0x5A, 0xA0, 0x3A, 0.5f, "Grass_Side"},   // 草侧
        {0x8B, 0x66, 0x38, 0.5f, "Dirt"},          // 泥土
        {0x8C, 0x8C, 0x8C, 0.6f, "Stone"},         // 石头
        {0xC2, 0xB8, 0x80, 0.3f, "Sand"},          // 沙子
        {0x6B, 0x42, 0x26, 0.5f, "Wood"},          // 木头
        {0x2E, 0x7D, 0x32, 0.7f, "Leaves"},        // 树叶
        {0x30, 0x60, 0xC8, 0.1f, "Water"},         // 水
        {0x33, 0x33, 0x33, 0.8f, "Bedrock"},       // 基岩
    };

    // 生成每个方块的纹理
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
    {
        int tileX = (i % TILES_X) * TILE_SIZE;
        int tileY = (i / TILES_X) * TILE_SIZE;

        uint8_t* tileData = atlas.data() + (tileY * ATLAS_W + tileX) * 4;
        generateBlockTexture(tileData, TILE_SIZE,
                             blocks[i].r, blocks[i].g, blocks[i].b,
                             blocks[i].roughness, 0.5f);

        printf("  图块 %zu: %s (%d, %d)\n", i, blocks[i].name, tileX, tileY);
    }

    printf("\n图集大小: %d×%d, 图块: %zu 个, 每图块 %d×%d\n",
           ATLAS_W, ATLAS_H, sizeof(blocks) / sizeof(blocks[0]), TILE_SIZE, TILE_SIZE);
    printf("输出: textures/atlas.png\n");

    // 实际输出应使用 stb_image_write:
    // stbi_write_png("textures/atlas.png", ATLAS_W, ATLAS_H, 4, atlas.data(), ATLAS_W * 4);
    printf("请链接 stb_image_write.h 并取消注释 stbi_write_png 调用\n");

    return 0;
}
