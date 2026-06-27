# 项目架构说明 —— AI 体素世界游戏（Vulkan + C++）

## 概述

本项目是一个使用 **C++17 + Vulkan 1.2+ + GLFW + GLM** 开发的体素世界游戏原型，构建系统使用 **CMake**。实现了自由移动、实时地形生成、方块注册表、碰撞检测，以及全部 7 项性能优化目标。

---

## 目录结构

```
├── CMakeLists.txt              # CMake 构建配置（自动查找依赖、编译着色器）
├── bin/                        # 可执行文件输出目录
├── build/                      # 构建中间文件
├── cmake/                      # CMake 辅助模块（环境诊断、MSYS2 查找等）
├── docs/
│   ├── ARCHITECTURE.md         # 本文档
│   └── VULKAN_MINGW_SETUP.md   # MinGW Vulkan 环境配置说明
├── shaders/
│   ├── vertex.glsl             # GLSL 顶点着色器
│   ├── fragment.glsl           # GLSL 片元着色器
│   ├── vertex.spv              # 编译后的 SPIR-V（由 glslc 生成）
│   ├── fragment.spv
│   └── compile_shaders.bat     # 手动编译着色器的批处理脚本
└── src/
    ├── main.cpp                # 程序入口
    ├── vulkan_app.h/.cpp       # 主应用类（整合所有模块的主循环）
    ├── block_registry.h        # 方块注册表（纯头文件，8 种默认方块）
    ├── noise.h                 # Perlin/Simplex 噪声（纯头文件）
    ├── thread_pool.h           # 线程池（纯头文件，支持优先级调度）
    ├── camera.h                # 第一人称摄像机（纯头文件）
    ├── chunk.h/.cpp            # 区块数据与网格构建
    ├── octree.h/.cpp           # 八叉树空间管理
    ├── terrain_generator.h/.cpp# 地形生成器
    ├── player.h/.cpp           # 玩家控制器（输入+碰撞）
    ├── world.h/.cpp            # 世界管理器
    ├── optimization_manager.h/.cpp # 性能优化管理器
    └── render_engine.h/.cpp    # Vulkan 渲染引擎
```

---

## 模块职责与数据流

### 数据流图

```
                        +------------------+
                        |   BlockRegistry  |
                        |  (方块属性注册表) |
                        +--------+---------+
                                 |
                                 v
+-----------+     +----------+   |   +------------------+
|  Player   |---->|  World   |---+-->| TerrainGenerator |
| (输入/碰  |     | (区块调  |       | (Perlin 噪声)    |
|  撞/摄像) |     |  度/管  |       +------------------+
+-----------+     |  理/缓存)|
                  |          |       +------------------+
                  |          |------>|   ThreadPool     |
                  |          |       | (异步优先级调度) |
                  +----+-----+       +------------------+
                       |
                       v
                  +----------+       +------------------+
                  |  Octree  |------>|  Frustum Culling |
                  | (空间查询)|       +------------------+
                  +----------+
                       |
                       v
         +---------------------------+
         |  OptimizationManager      |
         | (LOD选择 + 合批 + 统计)   |
         +------------+--------------+
                      |
                      v
         +---------------------------+
         |  RenderEngine             |
         | (Vulkan 管线 + 绘制提交)  |
         +---------------------------+
```

### 各模块详解

#### 1. `BlockRegistry`（方块注册表）
- **文件**: `block_registry.h`
- **职责**: 管理所有方块类型的定义，运行时动态扩展
- **数据格式**: 每个方块 `uint16_t` 类型 ID + 属性结构体
- **预定义方块**: 空气、草、泥土、石头、沙子、木头、树叶、水、基岩（8+ 种）
- **关键设计**: `BlockData` 结构体包含 `isSolid`、`isTransparent`、`color`、`hardness` 等属性

#### 2. `Chunk`（区块）
- **文件**: `chunk.h/.cpp`
- **职责**: 存储 16×16×16 个方块的紧凑数据，提供网格构建
- **紧凑格式**: 每个方块 `uint16_t`（类型）+ `uint8_t`（元数据）= 3 字节，区块 ≈ 12KB
- **LOD 支持**: 3 个级别（LOD0: 全分辨率, LOD1: 2×2×2, LOD2: 4×4×4）
- **全局缓存**: 静态 `unordered_map` 以 64 位 key 索引所有已加载区块

#### 3. `MeshBuilder`（网格构建器）
- **文件**: `chunk.h/.cpp` (内嵌在 Chunk 模块中)
- **核心算法**: 背面剔除 —— 对每个方块仅生成与空气/透明方块相邻的面
- **LOD 合并**: 将多个方块合并为一个"宏方块"，取多数决类型 + 平均颜色
- **顶点格式**: `VoxelVertex { position, normal, color, ao }`

#### 4. `TerrainGenerator`（地形生成器）
- **文件**: `terrain_generator.h/.cpp`
- **算法**: Perlin 噪声 fBm（分形布朗运动）
- **地形层次**: 基岩 → 深层石头（含洞穴）→ 近地表泥土 → 地表（草/沙）→ 水
- **洞穴系统**: 3D Perlin 噪声阈值判定
- **参数可调**: 振幅、频率、倍频程、海平面高度、洞穴阈值

#### 5. `Octree`（八叉树）
- **文件**: `octree.h/.cpp`
- **职责**: 组织所有已加载区块，支持快速空间查询
- **查询类型**: 视锥体相交测试、球体相交测试、邻近查询
- **节点分裂**: 超过 4 个区块的节点自动细分（最大深度 8）
- **剔除流程**: 节点级粗筛 → 区块级精筛（AABB-平面测试）

#### 6. `Player`（玩家控制器）
- **文件**: `player.h/.cpp`
- **物理参数**: 速度 4.5 m/s（冲刺 7.0）、跳跃 8.0 m/s、重力 25.0 m/s²
- **碰撞体**: AABB 0.6×1.8×0.6（宽×高×宽）
- **碰撞算法**: 分别处理 X/Y/Z 三轴，支持沿墙面滑动
- **死亡保护**: 跌落至 y < -100 时重生

#### 7. `World`（世界管理器）
- **文件**: `world.h/.cpp`
- **职责**: 核心调度器，管理区块生命周期
- **加载范围**: 水平 10 区块 × 垂直 6 区块（可配置）
- **异步生成**: 使用 `ThreadPool` 后台生成地形 → 构建网格
- **优先级调度**: 按距离分为 HIGH/NORMAL/LOW 三级
- **增量更新**: 只在玩家移动到新区块时触发更新

#### 8. `OptimizationManager`（优化管理器）
- **文件**: `optimization_manager.h/.cpp`
- **LOD 分区**:
  - 近区 (0-80): LOD0 全分辨率 + 全光照
  - 中区 (80-200): LOD1 2×2×2 合并 + 简化光照
  - 远区 (200-500): LOD2 4×4×4 合并 + 仅颜色
- **合批**: 将同一 LOD 级别的所有可见区块合并到单个 `BatchData`
- **统计**: 区块数、顶点数、三角面数、Draw Call 数

#### 9. `RenderEngine`（渲染引擎）
- **文件**: `render_engine.h/.cpp`
- **Vulkan 功能**: 交换链、图形管线、顶点/索引缓冲、统一缓冲、描述符、同步
- **渲染流程**: 等待栅栏 → 获取图像 → 更新 UBO → 上传顶点 → 录制命令 → 提交 → 呈现
- **着色器**: 支持基础光照（环境光 + 漫反射）、简单雾效
- **缓冲区管理**: 动态扩容的顶点/索引缓冲区（预留 50% 余量）

#### 10. `ThreadPool`（线程池）
- **文件**: `thread_pool.h`（纯头文件）
- **特点**: 固定工作线程、优先级队列（最小堆）、RAII 生命周期管理
- **优先级**: HIGH（玩家周围 3 区块内）、NORMAL（3-6 区块）、LOW（6 区块外）

---

## 性能优化具体实现

### 1. 背面剔除（Back-face Culling）
- **位置**: `MeshBuilder::shouldGenerateFace()` 在 `chunk.cpp`
- **原理**: 遍历区块中每个非空气方块，对 6 个方向检查相邻方块
- **决策**: 相邻为空气或透明方块 → 生成该面的三角形；相邻为固体 → 跳过
- **LOD 适配**: 对于合并后的宏方块，检查相邻宏方块区域的全部子方块

### 2. 视锥体剔除（Frustum Culling）
- **位置**: `OctreeNode::queryFrustum()` 在 `octree.cpp` + `Camera::getFrustumPlanes()` 在 `camera.h`
- **原理**: 
  1. 从 View-Projection 矩阵提取 6 个视锥体平面（Gribb-Hartmann 方法）
  2. 对每个八叉树节点执行 AABB-平面相交测试（P-顶点法）
  3. 节点完全在视锥体外 → 整棵子树跳过
  4. 节点相交 → 对内部每个区块做精确 AABB 测试

### 3. 远景体素 LOD 渲染
- **位置**: `OptimizationManager::selectLOD()` + `MeshBuilder::buildMesh()`
- **LOD0 (全分辨率)**: 每个方块 1x1x1，6 个面独立生成
- **LOD1 (2×2×2)**: 将 8 个方块视为一个宏方块，生成合并后的面
- **LOD2 (4×4×4)**: 将 64 个方块视为一个宏方块
- **选择策略**: 根据区块中心到玩家的距离选择 LOD

### 4. 远近分区差异化渲染
- **位置**: `optimization_manager.h` 中的 `ZONE_NEAR/MEDIUM/FAR` 常量
- **近区 (0-80)**: LOD0 + 完整着色器（光照+雾效）
- **中区 (80-200)**: LOD1 + 简化着色器
- **远区 (200-500)**: LOD2 + 极简着色器（仅颜色，无光照）
- **实现**: 可在 `createGraphicsPipeline()` 中为不同区域创建多套管线

### 5. 八叉树三维空间数据管理
- **位置**: `octree.h/.cpp`
- **结构**: 递归 8 分体，每个节点覆盖一个轴对齐立方体
- **分裂条件**: 节点内区块数 > 4 且深度 < 最大深度（8）
- **查询优化**: 
  - 视锥体测试：先粗筛节点，再精筛区块
  - 距离排序：查询结果按距离升序排列

### 6. 异步后台生成 + 缓存调度
- **位置**: `World::generateChunkAsync()` + `ThreadPool`
- **流程**: 玩家移动 → 计算需要的区块 → 按优先级提交生成任务 → 地形生成 → 自动提交网格构建 → 标记 READY
- **优先级**: HIGH（玩家周围 3 区块） / NORMAL / LOW
- **缓存**: `Chunk::s_chunkCache` 全局缓存，`World::m_loadedChunkKeys` 跟踪已加载

### 7. 几何体合批统一渲染提交
- **位置**: `OptimizationManager::batchChunks()` + `RenderEngine::recordCommandBuffer()`
- **策略**: 
  1. 将同一 LOD 级别的所有可见区块顶点/索引合并到连续内存
  2. 使用单个 Vulkan 顶点/索引缓冲区上传
  3. 每个 LOD 级别一次 `vkCmdDrawIndexed()` 调用
  4. 最多 3 个 Draw Call（每个 LOD 级别一个）

---

## 编译步骤

### 前置要求
1. **Vulkan SDK** 1.2+（从 [LunarG](https://vulkan.lunarg.com/) 下载）
2. **CMake** 3.15+
3. **编译器**: TDM-GCC（MinGW-w64）或 MSVC
4. **GLFW**: 通过 MSYS2 安装 (`pacman -S mingw-w64-ucrt-x86_64-glfw`) 或手动设置 `GLFW_DIR`
5. **GLM**: 通过 MSYS2 安装 (`pacman -S mingw-w64-ucrt-x86_64-glm`) 或手动设置 `GLM_INCLUDE_DIR`

### 编译命令
```bash
# 1. 配置
mkdir build && cd build
cmake .. -G "MinGW Makefiles"

# 2. 编译
cmake --build .

# 3. 编译着色器（若 CMake 未自动编译）
cd shaders
compile_shaders.bat
cd ..

# 4. 运行
bin/VulkanApp.exe
```

---

## 已知限制与未来改进

1. **纹理**: 当前使用程序化生成的纯色方块，未来可替换为纹理图集
2. **多线程安全**: World 类的 `this` 捕获在线程池任务中存在生命周期风险，可改用 `shared_ptr` 管理
3. **区块卸载**: 卸载时八叉树移除和内存回收需要更完善的协调
4. **洞穴生成**: 3D 洞穴噪声过于简单，可使用连通性算法生成更自然的洞穴
5. **生态域**: 当前未实现不同生态域，可通过温度/湿度噪声扩展
6. **网络同步**: 未实现多人联机功能
