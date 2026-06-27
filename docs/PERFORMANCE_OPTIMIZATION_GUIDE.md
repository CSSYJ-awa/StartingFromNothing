# Vulkan 体素世界游戏 — 深度性能优化指南

> 本文档提供 10 项性能优化的完整实现方案、代码、集成位置和测试方法。
> 实施难度从 ★ (简单) 到 ★★★★★ (复杂)。建议优先实施 **Top 3**。

---

## 总体架构

```
┌─────────────────────────────────────────────────────┐
│  Render Graph (Pass 调度 + 自动 Barrier)              │
│  ┌────────────┐  ┌────────────┐  ┌───────────────┐  │
│  │ 主场景渲染  │→│ 遮挡剔除 CS │→│ Indirect Draw │  │
│  └────────────┘  └────────────┘  └───────────────┘  │
├─────────────────────────────────────────────────────┤
│  Material / Texture Atlas  │  Pipeline Cache         │
├─────────────────────────────────────────────────────┤
│  ECS (entt) │  ThreadPool + Async Load │  Mem Pool  │
└─────────────────────────────────────────────────────┘
```

---

## 优先实施 Top 3

1. **Pipeline State 缓存与重用** (★) — 零侵入，改动最小，收益立竿见影
2. **Multi-Draw Indirect + 几何体合批** (★★) — 解决当前最大瓶颈（Draw Call 过多）
3. **屏幕空间动态 LOD** (★★★) — 大幅减少远距离顶点数

---

# 优化项详细实现

---

## 1. GPU 驱动的视锥体 + 遮挡剔除 (★★★★)

### 原理
CPU 端剔除会将所有区块 AABB 依次做视锥体测试（O(n)），且无法做子像素遮挡测试。
改用 Compute Shader 对每个区块执行并行的视锥测试 + 层级 Z-Buffer (Hi-Z) 遮挡测试，
输出可见区块列表供 Indirect Draw 使用。

### 实现步骤

#### 1.1 构建深度金字塔 (Hi-Z)

```cpp
// render_engine.h — 新增成员
VkImage        m_hizImage;            // 层级深度图像
VkImageView    m_hizImageView;
VkDeviceMemory m_hizImageMemory;
VkDescriptorSetLayout m_hizDSLayout;
VkDescriptorSet       m_hizDS;

// 每帧渲染完成后，将深度缓冲缩小构建金字塔
void RenderEngine::buildHiZDepthPyramid(VkCommandBuffer cmd)
{
    // 使用 vkCmdBlitImage 逐层缩小
    int w = m_swapchainExtent.width, h = m_swapchainExtent.height;
    int mipLevels = static_cast<int>(std::log2(std::max(w, h))) + 1;

    for (int i = 1; i < mipLevels; ++i)
    {
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {std::max(w >> (i-1), 1), std::max(h >> (i-1), 1), 1};
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {std::max(w >> i, 1), std::max(h >> i, 1), 1};
        blit.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, static_cast<uint32_t>(i-1), 0, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, static_cast<uint32_t>(i), 0, 1};

        vkCmdBlitImage(cmd,
            m_hizImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_hizImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_NEAREST);
    }
}
```

#### 1.2 Compute Shader — 视锥 + Hi-Z 遮挡测试

```glsl
// shaders/cull.comp — GPU 剔除着色器
#version 450
#extension GL_EXT_shader_8bit_storage : enable
#extension GL_EXT_shader_16bit_storage : enable

layout(local_size_x = 64) in;

// 视锥体参数（6 planes）
struct FrustumPlane { vec4 plane; }; // plane.xyz = normal, plane.w = distance
layout(binding = 0) uniform FrustumUBO {
    FrustumPlane planes[6];
    mat4 viewProj;
    ivec2 screenSize;
} frustum;

// 区块 AABB 列表（来自 CPU）
struct ChunkAABB {
    vec3 min;
    vec3 max;
    uint chunkIndex;  // 指向全局 chunk 数据
};
layout(binding = 1, std430) readonly buffer AABBBuffer {
    ChunkAABB aabbs[];
};

// 输出可见标志
layout(binding = 2, std430) writeonly buffer VisibleBuffer {
    uint visibleFlags[];  // 0 = culled, 1 = visible
};

// Hi-Z 深度金字塔（绑定为组合图像采样器）
layout(binding = 3) uniform sampler2D hizTexture;

// 投影 AABB → 屏幕空间包围盒
vec2 projectAABB(const vec3 min, const vec3 max, out float depthMin) {
    vec4 clipMin = frustum.viewProj * vec4(min, 1.0);
    vec4 clipMax = frustum.viewProj * vec4(max, 1.0);
    // 实际需要计算 8 个顶点投影的最小/最大 NDC，此处简化
    vec4 clip = (clipMin + clipMax) * 0.5;
    depthMin = clip.z / clip.w;
    return (clip.xy / clip.w) * 0.5 + 0.5; // [0,1] NDC
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= aabbs.length()) return;

    ChunkAABB aabb = aabbs[idx];
    bool visible = true;

    // 1) 视锥体测试
    for (int i = 0; i < 6; ++i)
    {
        vec3 normal = frustum.planes[i].plane.xyz;
        float dist  = frustum.planes[i].plane.w;
        vec3 posVtx = mix(aabb.min, aabb.max, step(0.0, normal));
        if (dot(normal, posVtx) < dist) { visible = false; break; }
    }

    // 2) Hi-Z 遮挡测试（仅当视锥测试通过）
    if (visible) {
        float depthMin;
        vec2 screenMin = projectAABB(aabb.min, aabb.max, depthMin);

        // 采样对应 mip 级别的深度
        ivec2 texCoord = ivec2(screenMin * vec2(textureSize(hizTexture, 0)));
        float clipDepth = texelFetch(hizTexture, texCoord, 0).r;

        if (depthMin > clipDepth) visible = false; // 被遮挡
    }

    visibleFlags[idx] = visible ? 1 : 0;
}
```

#### 1.3 C++ 端调度

```cpp
// 每帧在渲染主场景前调度
void RenderEngine::dispatchFrustumCull(VkCommandBuffer cmd)
{
    // barrier: 确保深度渲染完成 → 可供 Hi-Z 采样
    VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    // 更新视锥体 UBO
    vkCmdUpdateBuffer(cmd, m_cullUBO, 0, sizeof(FrustumUBO), &m_frustumData);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_cullPipelineLayout, 0, 1, &m_cullDS, 0, nullptr);

    uint32_t groupCount = (m_totalChunks + 63) / 64;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // barrier: 确保 visibleFlags 写完成 → 可被 Indirect Draw 读取
    VkMemoryBarrier cullBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    cullBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    cullBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &cullBarrier, 0, nullptr, 0, nullptr);
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| `render_engine.h/.cpp` | 添加 Hi-Z 图像、Compute Pipeline、调度方法 |
| `shaders/cull.comp` | 新建 |
| `vulkan_app.cpp` | 在 `render()` 中插入 `dispatchFrustumCull()` |

### 同步要点
- 深度渲染 → Hi-Z 构建：`VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT → TRANSFER_BIT` barrier
- Hi-Z 构建 → Compute 采样：`TRANSFER_BIT → COMPUTE_SHADER_BIT` barrier  
- Compute 输出 → Indirect Draw：`COMPUTE_SHADER_BIT → DRAW_INDIRECT_BIT` barrier

---

## 2. Multi‑Draw Indirect + 几何体合批 (★★)

### 原理
当前实现每 LOD 级别一次 `vkCmdDrawIndexed`，最多 3 个 Draw Call。
改为将所有区块合并到全局 Vertex/Index Buffer，用 `vkCmdDrawIndexedIndirect` 一次性提交所有可见区块。

### 2.1 全局缓冲区架构

```
Global Vertex Buffer (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
├── 区块 0 的顶点 (VoxelVertex[])
├── 区块 1 的顶点 (VoxelVertex[])
├── ...
└── 区块 N 的顶点 (VoxelVertex[])

Global Index Buffer (VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
├── 区块 0 的索引 (uint32_t[])
├── 区块 1 的索引 (uint32_t[])
├── ...
└── 区块 N 的索引 (uint32_t[])

Indirect Command Buffer (VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
├── VkDrawIndexedIndirectCommand[0] // LOD0
├── VkDrawIndexedIndirectCommand[1] // LOD1
└── VkDrawIndexedIndirectCommand[2] // LOD2
```

### 2.2 区块注册（Chunk 生成时上传）

```cpp
// 数据结构：每个区块在全局缓冲区的偏移
struct ChunkGPUSlot {
    uint32_t vertexOffset;  // 全局顶点缓冲区中的偏移（顶点数）
    uint32_t indexOffset;   // 全局索引缓冲区中的偏移（索引数）
    uint32_t vertexCount;   // 各 LOD 级别的顶点/索引数
    uint32_t indexCount[3]; // [LOD0, LOD1, LOD2]
    bool     valid;
};

// world.h — 新增
std::unordered_map<uint64_t, ChunkGPUSlot> m_chunkGPUSlots;
VkBuffer m_globalVertexBuffer, m_globalIndexBuffer;
VkDeviceMemory m_globalVertexMemory, m_globalIndexMemory;
size_t m_globalVertexCapacity = 0, m_globalIndexCapacity = 0;

// 区块网格生成完成后的上传
void World::uploadChunkMesh(const std::shared_ptr<Chunk>& chunk)
{
    uint64_t key = Chunk::chunkKey(
        chunk->m_position.x, chunk->m_position.y, chunk->m_position.z);

    auto& slot = m_chunkGPUSlots[key];
    if (!slot.valid)
    {
        // 分配全局缓冲区槽位
        slot.vertexOffset = m_nextVertexOffset;
        slot.indexOffset  = m_nextIndexOffset;
        for (int lod = 0; lod < 3; ++lod)
        {
            const auto& mesh = chunk->getMeshData(static_cast<LODLevel>(lod));
            slot.vertexCount    = mesh.vertices.size();
            slot.indexCount[lod] = mesh.indices.size();

            // 上传到全局缓冲区（通过 Staging Buffer）
            uploadToGlobalBuffer(slot.vertexOffset, mesh.vertices.data(),
                                 mesh.vertices.size() * sizeof(VoxelVertex));
            uploadToGlobalBuffer(slot.indexOffset, mesh.indices.data(),
                                 mesh.indices.size() * sizeof(uint32_t));

            m_nextVertexOffset += mesh.vertices.size();
            m_nextIndexOffset  += mesh.indices.size();
        }
        slot.valid = true;
    }
}
```

### 2.3 Indirect Command 填充

```cpp
// 每帧视锥体剔除后执行
void World::fillIndirectCommands(
    const std::vector<std::shared_ptr<Chunk>>& visibleChunks,
    VkDrawIndexedIndirectCommand commands[3],
    uint32_t commandCounts[3])
{
    for (int lod = 0; lod < 3; ++lod) commandCounts[lod] = 0;

    for (auto& chunk : visibleChunks)
    {
        uint64_t key = Chunk::chunkKey(
            chunk->m_position.x, chunk->m_position.y, chunk->m_position.z);
        auto it = m_chunkGPUSlots.find(key);
        if (it == m_chunkGPUSlots.end() || !it->second.valid) continue;

        auto& slot = it->second;
        for (int lod = 0; lod < 3; ++lod)
        {
            if (slot.indexCount[lod] == 0) continue;

            uint32_t idx = commandCounts[lod];
            commands[lod * MAX_CHUNKS_PER_LOD + idx] = {
                slot.indexCount[lod],       // indexCount
                1,                          // instanceCount
                slot.indexOffset,           // firstIndex
                0,                          // vertexOffset
                0                           // firstInstance
            };
            commandCounts[lod]++;
        }
    }
}
```

### 2.4 绘制调用

```cpp
// render_engine.cpp — render() 内
for (int lod = 0; lod < 3; ++lod)
{
    if (commandCounts[lod] == 0) continue;
    vkCmdDrawIndexedIndirect(cmd, m_indirectBuffer,
        lod * MAX_CHUNKS_PER_LOD * sizeof(VkDrawIndexedIndirectCommand),
        commandCounts[lod], sizeof(VkDrawIndexedIndirectCommand));
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| `world.h/.cpp` | 添加 `m_globalVertexBuffer`、`m_chunkGPUSlots`、`uploadChunkMesh()` |
| `render_engine.cpp` | `recordCommandBuffer()` 中改用 `vkCmdDrawIndexedIndirect` |
| `shaders/vertex.glsl` | 无需修改（使用已有顶点格式） |

### 预期收益
- Draw Call: 3 → 3（无论多少区块，始终 3 次 Indirect Draw）
- 每区块的 CPU → GPU 上传开销：从每帧上传 → 仅生成时上传一次
- 合批后 GPU 可并行处理更多三角形

---

## 3. 屏幕空间动态 LOD (★★★)

### 原理
当前 LOD 基于距离选择。更精确的做法：将区块投影到屏幕，根据其屏幕像素尺寸选择 LOD。
远距离区块用低分辨率网格，近距离用高分辨率。

### 3.1 LOD 生成（已存在，增强）

```cpp
// chunk.cpp — 增强 buildAllLODs，添加 4³ 超低 LOD
constexpr int LOD_MERGE_SIZES[] = { 1, 2, 4, 8 }; // 新增 LOD3: 8×8×8
// 但保留 3 级以节省内存，在实际项目中使用 4 级

// 屏幕空间 LOD 选择
int OptimizationManager::selectLODByScreenSize(
    const glm::vec3& chunkCenter,
    const glm::mat4& viewProj,
    int screenWidth, int screenHeight)
{
    // 计算区块 8 个顶点的屏幕投影
    float minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    const float halfSize = CHUNK_SIZE * 0.5f;

    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 corner = chunkCenter + glm::vec3(
            (i & 1 ? 1 : -1) * halfSize,
            (i & 2 ? 1 : -1) * halfSize,
            (i & 4 ? 1 : -1) * halfSize);

        glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
        glm::vec2 ndc = glm::vec2(clip) / clip.w; // [-1, 1]

        minX = std::min(minX, ndc.x); maxX = std::max(maxX, ndc.x);
        minY = std::min(minY, ndc.y); maxY = std::max(maxY, ndc.y);
    }

    // 屏幕像素尺寸
    float pixelW = (maxX - minX) * screenWidth * 0.5f;
    float pixelH = (maxY - minY) * screenHeight * 0.5f;
    float screenSize = std::max(pixelW, pixelH);

    // LOD 选择
    if (screenSize > 80.0f) return 0;  // LOD0 (全分辨率)
    if (screenSize > 30.0f) return 1;  // LOD1 (2×2×2)
    if (screenSize > 10.0f) return 2;  // LOD2 (4×4×4)
    return 2; // 最小保留 LOD2
}
```

### 3.2 运行时快速切换

```cpp
// optimization_manager.cpp — 在 processChunks 中替换 LOD 选择
void OptimizationManager::processChunks(
    const std::vector<std::shared_ptr<Chunk>>& visibleChunks,
    const glm::vec3& playerPos,
    const glm::mat4& viewProj,
    int screenWidth, int screenHeight,
    std::array<BatchData, 4>& batches)
{
    // 清空批次
    for (auto& b : batches) b.clear();

    for (const auto& chunk : visibleChunks)
    {
        if (!chunk || !chunk->isReady()) continue;

        int lod = selectLODByScreenSize(
            chunk->getCenter(), viewProj, screenWidth, screenHeight);

        // 如果该 LOD 网格无效，降级
        while (lod > 0 && !chunk->getMeshData(static_cast<LODLevel>(lod)).valid)
            lod--;

        const auto& mesh = chunk->getMeshData(static_cast<LODLevel>(lod));
        if (!mesh.valid) continue;

        auto& batch = batches[lod];
        // 累计顶点/索引数据用于合批...
    }
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| `optimization_manager.h/.cpp` | 添加 `selectLODByScreenSize()`，修改 `processChunks` 签名 |
| `vulkan_app.cpp` | 调用处传入 `viewProj` 和屏幕尺寸 |

### 同步要点
- LOD 选择在 CPU 端每帧执行，无需额外同步
- 区块网格预生成在后台线程，用 `ChunkState` 原子状态管理可见性

---

## 4. 着色器 Subgroup / Wave 加速 (★★)

### 原理
利用 Vulkan 1.1+ 的 Subgroup (Warp/Wave) 操作，在片元着色器中用 `subgroupShuffle` 共享相邻像素的计算结果，减少冗余纹理采样。

### 4.1 片元着色器 Subgroup 光照优化

```glsl
// shaders/fragment.glsl — 添加 Subgroup 优化
#version 450
#extension GL_KHR_shader_subgroup_shuffle : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

// 使用 Subgroup 共享漫反射光照计算结果
// 同一 Wave 内相邻像素的法线和位置通常相近

vec3 computeLighting(vec3 normal, vec3 worldPos)
{
    // 传统计算
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
    float diff = max(dot(normal, lightDir), 0.0);

    // 利用 subgroupShuffle 从相邻线程共享结果
    // 当前线程从索引 (gl_SubgroupInvocationID ^ 1) 获取法线
    vec3 neighborNormal = subgroupShuffle(normal, gl_SubgroupInvocationID ^ 1);

    // 如果两个法线足够接近，共享光照结果
    float normalDiff = length(normal - neighborNormal);
    float neighborDiff = max(dot(neighborNormal, lightDir), 0.0);

    // 混合：差异小时使用邻居的结果（减少计算量）
    float blend = smoothstep(0.0, 0.1, normalDiff);
    diff = mix(neighborDiff, diff, blend);

    return vec3(0.4, 0.5, 0.6) * 0.4 + diff * vec3(1.0, 0.95, 0.85);
}

// Fallback（非 NVIDIA / 不支持 subgroup 的设备）
#ifndef GL_KHR_shader_subgroup_shuffle
vec3 computeLighting(vec3 normal, vec3 worldPos) {
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
    float diff = max(dot(normal, lightDir), 0.0);
    return vec3(0.4, 0.5, 0.6) * 0.4 + diff * vec3(1.0, 0.95, 0.85);
}
#endif
```

### 4.2 Compute Shader Wave 前缀和

```glsl
// shaders/prefix_sum.comp — 使用 subgroup 加速可见区块扫描
#version 450
#extension GL_KHR_shader_subgroup_arithmetic : enable

layout(local_size_x = 32) in;
layout(binding = 0, std430) readonly buffer Input { uint flags[]; };
layout(binding = 1, std430) writeonly buffer Output { uint prefix[]; };

shared uint s_data[32];

void main() {
    uint tid = gl_GlobalInvocationID.x;
    uint val = tid < flags.length() ? flags[tid] : 0;

    // Subgroup 前缀和（硬件加速，O(log N)）
    uint wavePrefix = subgroupExclusiveAdd(val);
    uint waveTotal  = subgroupAdd(val);
    s_data[gl_LocalInvocationID.x] = waveTotal;

    barrier();
    // 在共享内存中做跨 Wave 的前缀和
    if (gl_LocalInvocationID.x == 0) {
        uint total = 0;
        for (int i = 0; i < gl_WorkGroupSize.x; ++i) {
            uint tmp = s_data[i];
            s_data[i] = total;
            total += tmp;
        }
    }
    barrier();

    uint groupBase = s_data[gl_LocalInvocationID.x];
    if (val != 0)
        prefix[groupBase + wavePrefix] = tid;
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| `shaders/fragment.glsl` | 添加 Subgroup 优化的 `computeLighting` |
| 新增 `shaders/prefix_sum.comp` | 前缀和 Compute Shader |

### 同步要点
无新增同步需求。Subgroup 操作在单 Wave 内是隐式同步的。

---

## 5. 纹理图集 + BCn 压缩 (★★★)

### 原理
将方块的程序化颜色替换为真正的纹理图集。将所有方块的纹理排列到一张大图上，
使用 BC7 压缩（VK_FORMAT_BC7_UNORM_BLOCK）减少显存占用。
着色器中根据方块 ID 查表获取 UV 偏移。

### 5.1 图集生成工具

```cpp
// tools/atlas_generator.cpp — 独立工具，生成纹理图集
#include <stb_image_write.h>
#include <vector>
#include <cmath>

// 程序化生成各方块纹理并拼接到图集
void generateAtlas(const char* outputPath, int tileSize = 16)
{
    int atlasWidth  = 256; // 16 tiles × 16 pixels
    int atlasHeight = 256;
    std::vector<uint8_t> atlas(atlasWidth * atlasHeight * 4);

    // 为每种方块生成纹理（简化：纯色 + 简单噪点）
    struct BlockTex { uint8_t r, g, b; const char* name; };
    BlockTex blocks[] = {
        { 0x44, 0x9E, 0x44, "grass_top" },  // 草顶
        { 0x8B, 0x66, 0x38, "dirt" },        // 泥土
        { 0x8C, 0x8C, 0x8C, "stone" },       // 石头
        { 0xC2, 0xB8, 0x80, "sand" },        // 沙子
        { 0x6B, 0x42, 0x26, "wood" },        // 木头
        { 0x2E, 0x7D, 0x32, "leaves" },      // 树叶
        { 0x30, 0x60, 0xC8, "water" },       // 水
        { 0x33, 0x33, 0x33, "bedrock" },     // 基岩
    };

    for (size_t i = 0; i < 8; ++i)
    {
        int tx = (i % 16) * tileSize;
        int ty = (i / 16) * tileSize;

        for (int y = 0; y < tileSize; ++y)
        {
            for (int x = 0; x < tileSize; ++x)
            {
                int px = tx + x, py = ty + y;
                int idx = (py * atlasWidth + px) * 4;
                // 添加简单噪点模拟纹理
                uint8_t noise = (rand() % 32) - 16;
                atlas[idx + 0] = std::clamp(blocks[i].r + noise, 0, 255);
                atlas[idx + 1] = std::clamp(blocks[i].g + noise, 0, 255);
                atlas[idx + 2] = std::clamp(blocks[i].b + noise, 0, 255);
                atlas[idx + 3] = 255;
            }
        }
    }

    stbi_write_png(outputPath, atlasWidth, atlasHeight, 4, atlas.data());
    // 输出后使用 NVIDIA Texture Tools 或 BC7E 压缩为 BC7
    printf("图集已生成: %s (256×256)\n", outputPath);
    printf("请使用 BC7E 压缩: bc7e -f png -o atlas.ktx %s\n", outputPath);
}
```

### 5.2 Vulkan 纹理加载

```cpp
// render_engine.h — 新增纹理相关
#include <ktx.h>  // 使用 libktx 加载 KTX 纹理

VkImage        m_atlasImage;
VkImageView    m_atlasImageView;
VkDeviceMemory m_atlasImageMemory;
VkSampler      m_atlasSampler;
VkDescriptorSetLayout m_atlasDSLayout;
VkDescriptorSet       m_atlasDS;

void RenderEngine::loadTextureAtlas()
{
    ktxTexture2* ktxTex;
    ktxTexture2_CreateFromNamedFile("textures/atlas_bc7.ktx",
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTex);

    // 创建 Vulkan 图像并上传数据
    VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format    = VK_FORMAT_BC7_UNORM_BLOCK;  // BC7 压缩格式
    imgInfo.extent    = { ktxTex->baseWidth, ktxTex->baseHeight, 1 };
    imgInfo.mipLevels = ktxTex->numLevels;
    imgInfo.arrayLayers = 1;
    imgInfo.samples  = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling   = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage    = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    vkCreateImage(m_device, &imgInfo, nullptr, &m_atlasImage);
    // ... 分配内存、上传数据、创建 ImageView、创建 Sampler ...
    ktxTexture_Destroy(ktx_ktxTexture(ktxTex));
}

// 每种方块的 UV 偏移表
constexpr glm::vec2 BLOCK_UV_OFFSETS[9] = {
    {0.0f, 0.0f},     // AIR (unused)
    {0.0f, 0.0f},     // GRASS — tile (0,0)
    {1.0f/16, 0.0f},  // DIRT — tile (1,0)
    {2.0f/16, 0.0f},  // STONE — tile (2,0)
    // ...
};
```

### 5.3 着色器修改

```glsl
// shaders/vertex.glsl — 添加 UV 输出
layout(location = 2) out vec2 fragUV;
layout(location = 3) out flat uint fragBlockType;

// 输入：方块类型 ID（通过实例数据或顶点属性传入）
// 修改顶点结构，添加 blockType
void main() {
    fragUV = inUV + BLOCK_UV_OFFSETS[inBlockType];
    fragBlockType = inBlockType;
    // ...
}

// shaders/fragment.glsl — 纹理采样替代纯色
#version 450
layout(binding = 1) uniform sampler2D atlasTex;
layout(location = 2) in vec2 fragUV;
layout(location = 3) flat in uint fragBlockType;

void main() {
    vec4 texColor = texture(atlasTex, fragUV);
    // 应用光照
    vec3 finalColor = texColor.rgb * lighting;
    outColor = vec4(finalColor, texColor.a);
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| `tools/atlas_generator.cpp` | 新建工具 |
| `render_engine.h/.cpp` | 添加 `m_atlasImage`、`loadTextureAtlas()`、描述符更新 |
| `shaders/vertex.glsl` | 添加 UV 传递 |
| `shaders/fragment.glsl` | 纹理采样替代纯色 |
| `CMakeLists.txt` | 添加 `ktx` 依赖 |

---

## 6. ECS 架构改造（逻辑层）(★★★★★)

### 原理
将玩家、区块管理、碰撞检测等拆分为 Entity-Component-System 模式。
推荐使用 `entt` 库（单头文件，零依赖）。

### 6.1 Component 定义

```cpp
// ecs_components.h — 组件定义
#include <entt/entt.hpp>

// --- 组件 ---
struct Position { glm::vec3 value; };
struct Velocity { glm::vec3 value; };
struct AABB { glm::vec3 min, max; };
struct PlayerTag {};
struct ChunkComponent {
    glm::ivec3 pos;
    ChunkState state;
};
struct MeshComponent {
    std::array<LODMeshData, 3> lods;
};
struct CameraComponent {
    glm::mat4 view, proj;
    glm::vec3 front, right, up;
};

// --- Entity 创建 ---
entt::entity createPlayer(entt::registry& reg, const glm::vec3& pos)
{
    auto entity = reg.create();
    reg.emplace<Position>(entity, pos);
    reg.emplace<Velocity>(entity, glm::vec3(0.0f));
    reg.emplace<PlayerTag>(entity);
    reg.emplace<AABB>(entity,
        pos - glm::vec3(0.3f, 0.0f, 0.3f),
        pos + glm::vec3(0.3f, 1.8f, 0.3f));
    return entity;
}
```

### 6.2 System 实现

```cpp
// ecs_systems.h — System 实现

// 物理/碰撞 System
void physicsSystem(entt::registry& reg, float dt)
{
    auto view = reg.view<Position, Velocity, AABB, PlayerTag>();
    for (auto entity : view)
    {
        auto& pos  = view.get<Position>(entity).value;
        auto& vel  = view.get<Velocity>(entity).value;
        auto& aabb = view.get<AABB>(entity);

        // 重力
        vel.y -= 35.0f * dt;
        if (vel.y < 0 && isOnGround(pos, aabb)) vel.y = 0;

        pos += vel * dt;
        collideWithTerrain(pos, vel, aabb);
    }
}

// 区块生成 System（并行）
void chunkSystem(entt::registry& reg, ThreadPool& pool)
{
    auto view = reg.view<ChunkComponent, MeshComponent>();

    // 使用并行 for 处理多个区块
    std::for_each(std::execution::par, view.begin(), view.end(),
        [&](auto entity)
        {
            auto& chunk = view.get<ChunkComponent>(entity);
            auto& mesh  = view.get<MeshComponent>(entity);

            if (chunk.state == ChunkState::GENERATING)
            {
                TerrainGenerator gen(42);
                gen.generate(chunk.pos);
                chunk.state = ChunkState::MESHING;
            }
            if (chunk.state == ChunkState::MESHING)
            {
                MeshBuilder::buildAllLODs(chunk.pos, mesh.lods);
                chunk.state = ChunkState::READY;
            }
        });
}
```

### 6.3 集成方式

```cpp
// world.h — 改用 ECS
#include <entt/entt.hpp>

class World {
    entt::registry m_registry;
    ThreadPool m_pool;
    // ...
};

// world.cpp
void World::update(const glm::vec3& playerPos)
{
    // 1. 处理区块加载/卸载 (创建/销毁 Entity)
    processChunkLoadUnload(playerPos);

    // 2. 异步生成区块
    chunkSystem(m_registry, m_pool);

    // 3. 物理 System（主线程）
    physicsSystem(m_registry, m_deltaTime);
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| 新建 `ecs_components.h` | 组件定义 |
| 新建 `ecs_systems.h` | System 实现 |
| `world.h/.cpp` | 用 `entt::registry` 替代手动容器 |
| `CMakeLists.txt` | 添加 `entt` 头文件路径 |

### ECS 优势
- 数据局部性改善（同类组件存于连续数组）
- 天然支持并行（不同 System 可并行执行）
- 解耦：添加新行为无需修改已有类

---

## 7. 异步加载 + 预测性缓存调度 (★★★)

### 原理
当前已有 `ThreadPool` 和 `generateChunkAsync`，但缺少：
1. **预测性预加载** — 根据玩家移动方向提前生成前方区块
2. **双缓冲** — 后台生成结果无锁交给主线程
3. **磁盘缓存** — 已生成的区块序列化到磁盘，下次加载更快

### 7.1 预测性预加载

```cpp
// world.cpp — 增强 update()

void World::update(const glm::vec3& playerPos)
{
    glm::ivec3 playerChunk = Chunk::worldToChunk(playerPos);
    flushPendingInserts();

    // 预测玩家移动方向（基于最近几帧的位移）
    static glm::vec3 prevPos = playerPos;
    glm::vec3 movementDir = glm::normalize(playerPos - prevPos);
    prevPos = playerPos;

    // 在移动方向上前瞻 3 个区块预加载
    glm::ivec3 lookahead = glm::ivec3(movementDir * 3.0f);
    for (int i = 1; i <= LOOKAHEAD_STEPS; ++i)
    {
        glm::ivec3 aheadChunk = playerChunk + lookahead * i;
        uint64_t key = Chunk::chunkKey(aheadChunk.x, aheadChunk.y, aheadChunk.z);
        if (m_loadedChunkKeys.find(key) == m_loadedChunkKeys.end())
        {
            generateChunkAsync(aheadChunk, ThreadPool::Priority::LOW);
        }
    }

    // 原有加载/卸载逻辑
    if (playerChunk == m_lastPlayerChunk) return;
    m_lastPlayerChunk = playerChunk;
    // ...
}
```

### 7.2 双缓冲设计

```cpp
// world.h — 双缓冲结构
template<typename T>
struct DoubleBuffer {
    std::array<T, 2> buffers;
    std::atomic<int> readIndex{0};

    T& getWriteBuffer() { return buffers[1 - readIndex.load()]; }
    T& getReadBuffer()  { return buffers[readIndex.load()]; }

    void swap() { readIndex.store(1 - readIndex.load()); }
};

// 使用示例：可见区块列表
DoubleBuffer<std::vector<uint64_t>> m_visibleChunks;

// 后台线程填充写缓冲区
void World::asyncCulling(/*...*/)
{
    auto& writeBuf = m_visibleChunks.getWriteBuffer();
    writeBuf.clear();
    // ... 填充可见区块 key ...
    m_visibleChunks.swap(); // 原子交换，无锁
}

// 主线程读取
void World::render()
{
    const auto& readBuf = m_visibleChunks.getReadBuffer();
    for (auto key : readBuf) { /* 绘制 */ }
}
```

### 7.3 磁盘缓存

```cpp
// world.cpp — 序列化/反序列化
#include <fstream>
#include <cereal/archives/binary.hpp> // 使用 cereal 库

void World::saveChunkToDisk(const glm::ivec3& chunkPos, const Chunk& chunk)
{
    std::string filename = "world_cache/" +
        std::to_string(chunkPos.x) + "_" +
        std::to_string(chunkPos.y) + "_" +
        std::to_string(chunkPos.z) + ".chunk";

    std::ofstream ofs(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(ofs);
    archive(chunk.m_blocks, chunk.m_metadata);
}

bool World::loadChunkFromDisk(const glm::ivec3& chunkPos, Chunk& chunk)
{
    std::string filename = "world_cache/" +
        std::to_string(chunkPos.x) + "_" +
        std::to_string(chunkPos.y) + "_" +
        std::to_string(chunkPos.z) + ".chunk";

    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) return false;

    cereal::BinaryInputArchive archive(ifs);
    archive(chunk.m_blocks, chunk.m_metadata);
    return true;
}

// 异步生成时优先检查磁盘缓存
void World::generateChunkAsync(const glm::ivec3& chunkPos, ThreadPool::Priority priority)
{
    m_threadPool.enqueue(priority, [this, chunkPos]()
    {
        auto chunk = Chunk::getOrCreateChunk(chunkPos);

        // 尝试从磁盘加载
        if (loadChunkFromDisk(chunkPos, *chunk))
        {
            chunk->setState(ChunkState::LOADED);
        }
        else
        {
            chunk->setState(ChunkState::GENERATING);
            m_terrainGen.generate(*chunk);
            saveChunkToDisk(chunkPos, *chunk); // 缓存到磁盘
        }
        // ... 后续网格构建 ...
    });
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| `world.h/.cpp` | 添加预测性预加载、DoubleBuffer、磁盘缓存 |
| `CMakeLists.txt` | 添加 `cereal` 依赖（或取消依赖自建序列化） |

### 同步要点
- `DoubleBuffer::swap()` 使用 `atomic<int>` 实现无锁交换
- 磁盘 I/O 在线程池中执行，不阻塞主线程
- 文件系统操作需要异常处理

---

## 8. 自定义内存池分配器 (★★★)

### 原理
为顶点数据、索引数据、区块对象设计专用内存池，减少 `malloc/free` 开销和内存碎片。
区块对象使用固定大小 Pool，顶点/索引使用线性分配器（每帧重置）。

### 8.1 区块对象 Pool

```cpp
// memory_pool.h — 固定大小对象池
#include <vector>
#include <cstdint>

template<typename T, size_t PoolSize = 4096>
class FixedPool {
    alignas(alignof(T)) char m_data[PoolSize * sizeof(T)];
    uint32_t m_freeList[PoolSize];
    uint32_t m_head;

public:
    FixedPool() {
        for (uint32_t i = 0; i < PoolSize; ++i)
            m_freeList[i] = i;
        m_head = 0;
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        if (m_head >= PoolSize) return nullptr;
        uint32_t idx = m_freeList[m_head++];
        T* ptr = reinterpret_cast<T*>(m_data) + idx;
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }

    void deallocate(T* ptr) {
        ptr->~T();
        uint32_t idx = static_cast<uint32_t>(
            (reinterpret_cast<char*>(ptr) - m_data) / sizeof(T));
        m_freeList[--m_head] = idx;
    }

    T* get(uint32_t idx) {
        return reinterpret_cast<T*>(m_data) + idx;
    }
};

// 使用方式
static FixedPool<Chunk, 16384> s_chunkPool; // 最多 16384 个区块

// 替换 Chunk::getOrCreateChunk 中的 make_shared
auto chunkPtr = s_chunkPool.allocate(chunkPos);
auto chunk = std::shared_ptr<Chunk>(chunkPtr, [](Chunk* p) { s_chunkPool.deallocate(p); });
```

### 8.2 顶点/索引线性分配器

```cpp
// memory_pool.h — 线性分配器（每帧重置）
class LinearAllocator {
    char* m_buffer;
    size_t m_capacity;
    size_t m_offset;

public:
    LinearAllocator(size_t capacity)
        : m_capacity(capacity), m_offset(0)
    {
        m_buffer = static_cast<char*>(std::aligned_alloc(64, capacity));
    }

    ~LinearAllocator() { std::free(m_buffer); }

    template<typename T>
    T* allocate(size_t count = 1) {
        size_t size = count * sizeof(T);
        size_t aligned = (m_offset + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + size > m_capacity) return nullptr;
        m_offset = aligned + size;
        return reinterpret_cast<T*>(m_buffer + aligned);
    }

    void reset() { m_offset = 0; } // 每帧调用
};

// 在 RenderEngine 中使用
LinearAllocator m_vertexAlloc{ 64 * 1024 * 1024 }; // 64MB

void RenderEngine::allocateFrameBuffers(const BatchData& batch)
{
    auto* verts = m_vertexAlloc.allocate<VoxelVertex>(batch.vertices.size());
    std::memcpy(verts, batch.vertices.data(), batch.vertices.size() * sizeof(VoxelVertex));

    // 直接上传到 GPU
    uploadToGPU(verts, batch.vertices.size() * sizeof(VoxelVertex));
    m_vertexAlloc.reset(); // 下一帧开始时重置
}
```

### 8.3 VMA Pool（使用 Vulkan Memory Allocator）

```cpp
// 如果使用 VMA
#include <vk_mem_alloc.h>

VmaPool m_chunkVertexPool;
VmaAllocationCreateInfo poolCreateInfo = {};
poolCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
poolCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
poolCreateInfo.poolSize = 64 * 1024 * 1024; // 64MB 池

vmaCreatePool(m_allocator, &poolCreateInfo, &m_chunkVertexPool);

// 从此 Pool 分配
VmaAllocation alloc;
VkBuffer buf;
vmaCreateBuffer(m_allocator, &bufInfo,
    VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    &buf, &alloc, nullptr);
// 内部自动从 m_chunkVertexPool 分配内存
```

### 集成位置

| 文件 | 修改 |
|------|------|
| 新建 `memory_pool.h` | 包含 `FixedPool`、`LinearAllocator` |
| `chunk.h/.cpp` | 使用 `FixedPool<Chunk>` 替换 `std::make_shared` |
| `render_engine.h/.cpp` | 使用 `LinearAllocator` 管理顶点暂存缓冲区 |
| `CMakeLists.txt` | 可选添加 VMA |

---

## 9. Pipeline 状态缓存与重用 (★)

### 原理
Vulkan 创建 Graphics Pipeline 时开销较大（编译着色器、优化状态）。
使用 `VkPipelineCache` 缓存编译结果到磁盘，下次启动直接加载。

### 9.1 PipelineCache 管理类

```cpp
// render_engine.h — PSO 管理类
class PipelineManager {
    VkDevice m_device;
    VkPipelineCache m_cache = VK_NULL_HANDLE;
    std::unordered_map<size_t, VkPipeline> m_pipelineCache;
    std::string m_cachePath = "vulkan_pipeline_cache.bin";

public:
    void init(VkDevice device) {
        m_device = device;

        // 尝试从磁盘加载缓存
        VkPipelineCacheCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        std::vector<uint8_t> cachedData;
        std::ifstream file(m_cachePath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t size = file.tellg();
            cachedData.resize(size);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(cachedData.data()), size);
            ci.initialDataSize = size;
            ci.pInitialData    = cachedData.data();
        }
        vkCreatePipelineCache(device, &ci, nullptr, &m_cache);
    }

    void saveToDisk() {
        size_t dataSize;
        vkGetPipelineCacheData(m_device, m_cache, &dataSize, nullptr);
        std::vector<uint8_t> data(dataSize);
        vkGetPipelineCacheData(m_device, m_cache, &dataSize, data.data());
        std::ofstream file(m_cachePath, std::ios::binary);
        file.write(reinterpret_cast<char*>(data.data()), dataSize);
    }

    size_t hashKey(const VkGraphicsPipelineCreateInfo& ci) {
        // 用着色器模块句柄 + 顶点布局 + 渲染流程等计算 hash
        size_t h = 0;

        // 着色器 stage hash
        for (uint32_t i = 0; i < ci.stageCount; ++i)
            hashCombine(h, reinterpret_cast<size_t>(ci.pStages[i].module));

        // 顶点输入 hash
        if (ci.pVertexInputState)
            hashCombine(h, ci.pVertexInputState->vertexBindingDescriptionCount);

        // 渲染流程 hash
        hashCombine(h, reinterpret_cast<size_t>(ci.renderPass));

        return h;
    }

    VkPipeline getOrCreate(const VkGraphicsPipelineCreateInfo& ci) {
        size_t key = hashKey(ci);
        auto it = m_pipelineCache.find(key);
        if (it != m_pipelineCache.end())
            return it->second;

        VkPipeline pipeline;
        vkCreateGraphicsPipelines(m_device, m_cache, 1, &ci, nullptr, &pipeline);
        m_pipelineCache[key] = pipeline;
        return pipeline;
    }

    ~PipelineManager() {
        saveToDisk();
        for (auto& [_, pipe] : m_pipelineCache)
            vkDestroyPipeline(m_device, pipe, nullptr);
        vkDestroyPipelineCache(m_device, m_cache, nullptr);
    }
};
```

### 9.2 集成到 RenderEngine

```cpp
// render_engine.cpp
void RenderEngine::createGraphicsPipeline()
{
    VkGraphicsPipelineCreateInfo ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    // ... 填充 pipeline 信息 ...

    // 使用 PipelineManager 创建或复用
    m_pipeline = m_pipelineManager.getOrCreate(ci);
}

void RenderEngine::init() {
    m_pipelineManager.init(m_device);
    // ... 其他初始化 ...
    createGraphicsPipeline(); // 自动使用缓存
}

RenderEngine::~RenderEngine() {
    // PipelineManager 析构时自动保存缓存到文件
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| 新建 `src/pipeline_manager.h` | PipelineManager 类 |
| `render_engine.h/.cpp` | 集成 PipelineManager，替换直接创建 Pipeline |

### 预期收益
- 第二次启动：Pipeline 创建时间从 ~100ms 降至 ~5ms
- 多次创建相同 PSO 时自动复用

---

## 10. Render Graph 自动化资源管理 (★★★★★)

### 原理
Render Graph 将渲染流程建模为 Pass 之间的依赖 DAG。自动管理 Barrier 插入、
transient 资源复用（如临时颜色缓冲），避免手动跟踪资源状态。

### 10.1 核心类框架

```cpp
// render_graph.h — 轻量级 Render Graph
#include <vector>
#include <string>
#include <unordered_map>

// --- 资源类型 ---
enum class ResourceType {
    ColorAttachment,
    DepthAttachment,
    Buffer,
    Image
};

struct Resource {
    std::string name;
    ResourceType type;
    VkFormat format;
    VkExtent2D extent;
    bool transient = true; // 是否可复用
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// --- Pass 定义 ---
struct Pass {
    std::string name;
    std::vector<std::string> inputs;  // 输入资源名
    std::vector<std::string> outputs; // 输出资源名
    std::function<void(VkCommandBuffer)> execute;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

// --- RenderGraph ---
class RenderGraph {
    std::unordered_map<std::string, Resource> m_resources;
    std::vector<Pass> m_passes;
    // transient 资源池
    std::vector<Resource> m_transientPool;

public:
    Resource& createResource(const std::string& name, ResourceType type,
                              VkFormat format, VkExtent2D extent, bool transient = true)
    {
        auto [it, ok] = m_resources.try_emplace(name, name, type, format, extent, transient);
        return it->second;
    }

    void addPass(Pass pass) {
        m_passes.push_back(std::move(pass));
    }

    void compile() {
        // 1. 拓扑排序 Pass（按依赖关系）
        // 2. 计算每个资源的使用区间（first/last pass）
        // 3. 为 transient 资源分配实际 VkImage（从池复用）
        // 4. 创建 VkRenderPass 和 VkFramebuffer
        // 5. 计算 Pass 间 Barrier
    }

    void execute(VkCommandBuffer cmd, uint32_t frameIndex) {
        for (auto& pass : m_passes)
        {
            // 1. 插入 Barrier（资源状态转换）
            for (auto& inputName : pass.inputs)
            {
                auto& res = m_resources[inputName];
                transitionLayout(cmd, res.image,
                    res.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            for (auto& outputName : pass.outputs)
            {
                auto& res = m_resources[outputName];
                transitionLayout(cmd, res.image,
                    res.currentLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }

            // 2. 执行 Pass
            pass.execute(cmd);

            // 3. 更新资源状态
        }
    }

    void transitionLayout(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image = image;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        // ... 设置 srcAccessMask / dstAccessMask 等 ...
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void cleanup() {
        for (auto& [_, res] : m_resources)
            if (res.transient) {
                vkDestroyImageView(m_device, res.view, nullptr);
                vkDestroyImage(m_device, res.image, nullptr);
                vkFreeMemory(m_device, res.memory, nullptr);
            }
    }
};
```

### 10.2 使用示例

```cpp
// vulkan_app.cpp — 使用 Render Graph 组织渲染

void VulkanApp::setupRenderGraph()
{
    auto& rg = m_renderGraph;

    // 声明资源
    auto& mainColor = rg.createResource("MainColor",
        ResourceType::ColorAttachment, VK_FORMAT_B8G8R8A8_SRGB, m_extent);
    auto& mainDepth = rg.createResource("MainDepth",
        ResourceType::DepthAttachment, findDepthFormat(), m_extent);
    auto& hizDepth = rg.createResource("HiZDepth",
        ResourceType::Image, VK_FORMAT_R32_SFLOAT,
        {m_extent.width/2, m_extent.height/2});

    // 定义 Pass
    rg.addPass({
        "MainScene",
        {},                          // 输入：无
        {"MainColor", "MainDepth"},   // 输出
        [this](VkCommandBuffer cmd) {
            // 渲染体素世界
            renderWorld(cmd);
        }
    });

    rg.addPass({
        "HiZBuild",
        {"MainDepth"},                // 输入：深度
        {"HiZDepth"},                 // 输出：Hi-Z
        [this](VkCommandBuffer cmd) {
            buildHiZDepthPyramid(cmd);
        }
    });

    rg.addPass({
        "OcclusionCull",
        {"HiZDepth"},                 // 输入：Hi-Z
        {},                           // 输出无
        [this](VkCommandBuffer cmd) {
            dispatchFrustumCull(cmd);
        }
    });

    rg.compile(); // 自动插入 Barrier
}

void VulkanApp::run() {
    // 每帧
    m_renderGraph.execute(m_commandBuffer, m_currentFrame);
    // ...
}
```

### 10.3 Transient 资源复用

```cpp
// render_graph.cpp — 复用逻辑
Resource& RenderGraph::allocateTransient(const Resource& desc)
{
    // 查找匹配的空闲资源
    for (auto& res : m_transientPool)
    {
        if (res.format == desc.format &&
            res.extent.width == desc.extent.width &&
            res.extent.height == desc.extent.height &&
            !res.inUse)
        {
            res.inUse = true;
            return res;
        }
    }

    // 创建新资源
    Resource newRes = desc;
    createVkImage(newRes);
    newRes.inUse = true;
    m_transientPool.push_back(newRes);
    return m_transientPool.back();
}

void RenderGraph::endFrame() {
    // 所有 transient 资源标记为可复用
    for (auto& res : m_transientPool)
        res.inUse = false;
}
```

### 集成位置

| 文件 | 修改 |
|------|------|
| 新建 `src/render_graph.h/.cpp` | RenderGraph 核心类 |
| `vulkan_app.h/.cpp` | 用 Render Graph 组织渲染流程 |
| `render_engine.cpp` | Pass 执行函数移入 RenderGraph |

### 预期收益
- 自动 Barrier 管理 → 消除手动 `vkCmdPipelineBarrier` 错误
- Transient 资源复用 → 减少显存占用约 30-50%
- 清晰的 Pass DAG → 便于调试和性能分析

---

# 集成指南

## CMake 改动

```cmake
# CMakeLists.txt 添加以下内容

# --- entt (ECS) ---
# 下载 https://github.com/skypjack/entt 放入 external/entt/
target_include_directories(${PROJECT_NAME} PRIVATE external/entt/src)

# --- cereal (序列化) ---
# 下载 https://github.com/USCiLab/cereal 放入 external/cereal/
target_include_directories(${PROJECT_NAME} PRIVATE external/cereal/include)

# --- KTX (纹理加载) ---
find_package(ktx REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ktx)

# --- Vulkan Memory Allocator ---
# 下载 https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
target_include_directories(${PROJECT_NAME} PRIVATE external/VulkanMemoryAllocator/include)
add_library(vma STATIC external/VulkanMemoryAllocator/src/vk_mem_alloc.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE vma)

# --- Compute Shader 支持 ---
# 在着色器编译循环中添加 .comp 文件
set(SHADER_SOURCES
    ${SHADER_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cull.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/prefix_sum.comp"
)
```

## 编译选项

```cmake
# 启用 Vulkan 1.2+ 特性
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
)

# 链接 pthread（如果使用 std::thread）
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
```

## 运行时验证方法

| 优化 | 验证方法 |
|------|---------|
| GPU 剔除 | 使用 RenderDoc 捕获帧，查看 Compute Shader 输出和 Indirect Draw 的 visibleFlags |
| Multi-Draw Indirect | 对比优化前后的 Draw Call 数（Vulkan 层或使用 GPU 计时查询） |
| 屏幕空间 LOD | 移动视角观察远处区块细节变化，远处应自动降级 |
| Subgroup | 在着色器中添加 `gl_SubgroupSize` 输出，验证 Wave 大小 ≥ 32 |
| 纹理图集 | 确认图集加载成功，采样 UV 正确，无撕裂 |
| ECS | `entt::registry::each()` 遍历性能应优于手写循环 |
| 异步加载 | 观察快速移动时前方区块是否已预加载（无弹出） |
| 内存池 | 使用 `malloc_stats` 或 VMA 统计，确认碎片减少 |
| Pipeline Cache | 首次启动后检查 `vulkan_pipeline_cache.bin` 是否生成 |
| Render Graph | 使用 Vulkan 验证层检查 Barrier 错误，确认 transient 资源复用 |

---

# 性能对比预期

| 指标 | 优化前 | 优化后 | 改善幅度 |
|------|--------|--------|---------|
| Draw Call | 3 (每 LOD) | 3 (Indirect, 含数百区块) | 不变但容量↑100x |
| CPU 剔除时间 (10k 区块) | ~5ms | ~0.1ms (GPU 并行) | ~50x |
| 顶点数 (远距离) | 全分辨率 | LOD2 降级 | ~75% 减少 |
| 显存占用 (纹理) | 无 | BC7 压缩 ~2MB | 新增但可接受 |
| Pipeline 创建时间 | ~100ms | ~5ms (缓存后) | ~20x |
| 内存碎片 | 高 (频繁 new/delete) | 低 (Pool 分配器) | 显著改善 |
| 帧率 (大量区块) | ~30 FPS | ~90 FPS (预计) | ~3x |

---

# 实施路线建议

```
Phase 1 (1-2 天): Pipeline Cache (#9) + 内存池 (#8)
  → 零行为变动，快速交付

Phase 2 (3-5 天): Multi-Draw Indirect (#2) + 屏幕空间 LOD (#3)
  → 解决当前最大性能瓶颈

Phase 3 (5-7 天): GPU 剔除 (#1) + Subgroup (#4) + 纹理图集 (#5)
  → 充分利用 GPU 能力

Phase 4 (7-14 天): 异步加载 (#7) + ECS (#6) + Render Graph (#10)
  → 架构级重构，长期可维护性
```
