# StartingFromNothing — Vulkan 开发入门框架

> **从零开始，用 Vulkan + GLFW + GLM 踏上图形编程之路。**  
> 适用于 Windows 10/11，使用 MinGW-w64 (MSYS2) 工具链 + CMake 构建。

---

## 目录

- [项目特性](#项目特性)
- [快速开始（推荐）](#快速开始推荐)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [手动安装](#手动安装)
- [构建与运行](#构建与运行)
- [脚本工具集](#脚本工具集)
- [CMake 构建架构](#cmake-构建架构)
- [常见问题](#常见问题)

---

## 项目特性

- ✅ **一键环境部署** — `setup_env.ps1` 自动检测 / 安装全部依赖
- ✅ **全自动 MSYS2 探测** — 自动定位 MSYS2 根目录，无需手动配置路径
- ✅ **MinGW 兼容 Vulkan** — 自动从 `vulkan-1.dll` 生成 MinGW 导入库
- ✅ **三层 GLFW 回退** — `find_package` → MSYS2 自动探测 → `find_library` 兜底
- ✅ **环境预检机制** — CMake 配置阶段自动诊断所有依赖
- ✅ **现代 CMake** — C++17、`target_*` 命令、`compile_commands.json`

---

## 快速开始（推荐）

### 1. 运行环境部署脚本

以 **管理员身份** 运行以下 PowerShell 脚本：

```powershell
# 在项目根目录下执行
.\scripts\setup_env.ps1
```

该脚本会自动完成以下工作：

| 组件 | 检测方式 | 自动安装？ |
|------|----------|:----------:|
| **MSYS2** | 环境变量 / 路径反推 | ❌ 打开下载页，引导安装 |
| **MinGW-w64** (GCC) | `where g++` | ✅ `pacman -S mingw-w64-x86_64-toolchain` |
| **CMake** | `where cmake` | ✅ `winget install CMake` |
| **GLFW** | 检查头文件 | ✅ `pacman -S mingw-w64-x86_64-glfw` |
| **GLM** | 检查头文件 | ✅ `pacman -S mingw-w64-x86_64-glm` |
| **Vulkan SDK** | 检查 `VULKAN_SDK` 环境变量 | ❌ 打开下载页，引导安装 |
| **Git** | `where git` | ✅ `winget install Git.Git` |

> **为什么 MSYS2 和 Vulkan SDK 需要手动安装？**  
> MSYS2 安装器需要用户交互（选择安装路径、架构），Vulkan SDK 需要用户勾选组件（开发组件非默认勾选）。强行静默安装可能导致不完整的环境。

### 2. 构建项目

```powershell
# 配置（自动检测编译器）
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug

# 构建
cmake --build build

# 运行
.\bin\VulkanApp.exe
```

### 3. 验证环境

```powershell
scripts\check_env.bat
```

---

## 目录结构

```
StartingFromNothing/
├── src/                    # C++ 源代码
│   ├── main.cpp            # 入口点
│   ├── vulkan_app.h        # VulkanApp 类声明
│   └── vulkan_app.cpp      # VulkanApp 类实现
├── cmake/                  # CMake 自定义模块
│   ├── CheckEnvironment.cmake        # 环境诊断（配置阶段执行）
│   ├── FindGLFW_MSYS2.cmake          # MSYS2 GLFW 自动探测
│   ├── FindGLM_MSYS2.cmake           # MSYS2 GLM 自动探测（纯头文件）
│   └── GenerateVulkanMingwLib.cmake  # MinGW 导入库自动生成
├── scripts/                # 辅助脚本
│   ├── setup_env.ps1                 # 【推荐】一键环境部署
│   ├── check_env.bat                 # 环境诊断（CMD）
│   ├── fix_msys2_glfw.bat            # MSYS2 GLFW/GLM 诊断向导
│   └── generate_vulkan_mingw_lib.bat # 手动生成 MinGW 导入库
├── shaders/                # 着色器源码 (GLSL) 与 SPIR-V 编译产物
├── assets/                 # 资源文件（纹理、3D 模型等）
├── third_party/            # 第三方库源码 (vendoring)
├── docs/                   # 项目文档
│   └── VULKAN_MINGW_SETUP.md       # Vulkan + MinGW 兼容性详解
├── bin/                    # 可执行文件输出目录（CMake 自动生成）
├── build/                  # CMake 构建中间文件（可删除重建）
├── .vscode/                # VS Code 配置
│   ├── settings.json
│   ├── launch.json
│   ├── tasks.json
│   └── c_cpp_properties.json
├── CMakeLists.txt          # CMake 主构建文件
├── LICENSE                 # 许可证
└── README.md               # 本文件
```

---

## 环境要求

| 组件 | 最低版本 | 备注 |
|------|:--------:|------|
| **CMake** | ≥ 3.15 | 支持 `-S` / `-B` 语法 |
| **MinGW-w64 (GCC)** | ≥ 8.0 | 支持 C++17，推荐 MSYS2 UCRT64 |
| **Vulkan SDK** | ≥ 1.3 | 提供 `vulkan.h` 和 `vulkan-1.dll` |
| **GLFW** | ≥ 3.3 | 窗口管理与输入处理 |
| **GLM** | ≥ 0.9.9 | 纯头文件数学库 |

### 推荐工具链

- **MSYS2 子系统**: `ucrt64`（推荐，兼容性最佳）
- **GCC 版本**: 本项目使用 GCC 15.2.0 (UCRT64)
- **Vulkan SDK**: 1.4.341.1

> 💡 **关于 MinGW 子系统选择**  
> 本项目当前 GLFW/GLM 安装在 `mingw64` 子系统，但编译器使用 `ucrt64`。  
> 建议统一到 `ucrt64`：`pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm`

---

## 手动安装

如果不想使用自动部署脚本，可以按以下步骤手动安装：

### 1. 安装 MSYS2

1. 访问 [https://www.msys2.org/](https://www.msys2.org/) 下载安装器
2. 安装到 `C:\msys64`（默认路径）
3. 打开 MSYS2 终端，更新包数据库：
   ```bash
   pacman -Syu
   ```

### 2. 安装 MinGW-w64 (UCRT64)

在 MSYS2 终端中执行：

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain --noconfirm
```

### 3. 安装 GLFW 和 GLM

在 MSYS2 终端中执行：

```bash
pacman -S mingw-w64-ucrt-x86_64-glfw --noconfirm
pacman -S mingw-w64-ucrt-x86_64-glm --noconfirm
```

### 4. 加入 PATH

将以下路径添加到系统 `PATH`（或用户 `PATH`）：

```
C:\msys64\ucrt64\bin
```

### 5. 安装 CMake

```bash
winget install CMake -e --silent
```

或从 [https://cmake.org/download/](https://cmake.org/download/) 下载安装。

### 6. 安装 Vulkan SDK

1. 访问 [https://vulkan.lunarg.com/sdk/home](https://vulkan.lunarg.com/sdk/home)
2. 下载并运行 `VulkanSDK-xxxx-Installer.exe`
3. **务必勾选** "Vulkan Runtime" 和 "Vulkan SDK Development Components"
4. 安装完成后，确认环境变量 `VULKAN_SDK` 已自动设置

### 7. 验证

```powershell
scripts\check_env.bat
```

---

## 构建与运行

### Debug 构建

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
.\bin\VulkanApp.exe
```

### Release 构建

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\bin\VulkanApp.exe
```

### 指定编译器路径

如果编译器不在默认 PATH 中，可以手动指定：

```powershell
cmake -S . -B build -G "MinGW Makefiles" ^
    -DCMAKE_CXX_COMPILER="D:/Program Files/msys64/ucrt64/bin/g++.exe" ^
    -DCMAKE_C_COMPILER="D:/Program Files/msys64/ucrt64/bin/gcc.exe" ^
    -DCMAKE_BUILD_TYPE=Debug
```

### 清理构建

```powershell
# 删除 build 目录重建
Remove-Item -Recurse -Force build
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
```

---

## 脚本工具集

### `scripts\setup_env.ps1` — 一键环境部署（推荐）

PowerShell 脚本，全自动检测并安装所有依赖。  
运行方式：右键 → "以管理员身份运行"。

### `scripts\check_env.bat` — 环境诊断

CMD 脚本，检测以下组件状态并给出诊断报告：

- CMake 版本 ≥ 3.15
- GCC ≥ 8.0 (C++17)
- Vulkan SDK + 头文件 + MinGW 导入库
- GLFW 头文件 + 库文件
- GLM 头文件

```bash
scripts\check_env.bat          # 交互模式
scripts\check_env.bat --no-pause  # 静默模式
```

### `scripts\fix_msys2_glfw.bat` — MSYS2 GLFW/GLM 诊断向导

自动定位 MSYS2 安装目录，检查各子系统（ucrt64 / mingw64 / clang64）下的 GLFW/GLM 安装情况，并提供 CMake 集成建议。

```bash
scripts\fix_msys2_glfw.bat
```

### `scripts\generate_vulkan_mingw_lib.bat` — MinGW 导入库生成器

从 `C:\Windows\System32\vulkan-1.dll` 生成 MinGW 兼容的导入库 `libvulkan-1.dll.a`。

```bash
scripts\generate_vulkan_mingw_lib.bat
scripts\generate_vulkan_mingw_lib.bat --no-pause
```

---

## CMake 构建架构

构建系统采用分层回退策略，确保在各种环境下都能正确链接依赖。

### GLFW 三层回退

```
[配置阶段]
    │
    ├─ 1. find_package(glfw3)  ─── 标准 CMake 配置文件
    │      失败 ↓
    ├─ 2. FindGLFW_MSYS2.cmake ─── 自动探测 MSYS2 路径
    │      失败 ↓
    └─ 3. find_library + find_path ─ 手动 / 环境变量回退
           失败 → FATAL_ERROR
```

### GLM 两层回退

```
[配置阶段]
    │
    ├─ 1. find_package(glm)  ──── 标准 CMake 配置文件
    │      失败 ↓
    └─ 2. FindGLM_MSYS2.cmake ─── 自动探测 MSYS2 路径（纯头文件）
           失败 → find_path 兜底 → FATAL_ERROR
```

### MinGW Vulkan 导入库自动生成

`GenerateVulkanMingwLib.cmake` 在配置阶段自动执行：

1. 检测编译器是否为 MinGW
2. 检查 `libvulkan-1.dll.a` 是否已存在
3. 若不存在，创建 `add_custom_target(generate_vulkan_mingw_lib)`
4. 构建时执行：`gendef` → `dlltool`，以 `C:\Windows\System32\vulkan-1.dll` 为源

> **为什么用 System32 的 vulkan-1.dll？**  
> 该 DLL 由显卡驱动程序安装，系统级可用，ABI 稳定，无需从 SDK 复制。  
> 详见 `docs/VULKAN_MINGW_SETUP.md`。

### 链接顺序（MinGW 注意事项）

```
Vulkan::Vulkan → ${GLFW_LIBRARIES} → gdi32 → opengl32 → m
```

MinGW 的 GNU ld 不会自动传递间接依赖，必须显式列出所有被依赖的系统库。

---

## 常见问题

### Q: CMake 报 `Invalid character escape '\P'`

**原因**: CMake 的 `message()` 会在 Windows 路径字符串中解析转义序列。  
**解决**: 使用 `file(TO_CMAKE_PATH ...)` 转换路径后再传入 `message()`，  
或使用正斜杠：`D:/Program Files/...`

### Q: `file(GLOB_RECURSE)` 导致"multiple definition of `main`"

**原因**: `GLOB_RECURSE` 会匹配 `build/` 目录下的 `CMakeCXXCompilerId.cpp`。  
**解决**: 改用 `file(GLOB SOURCES "src/*.cpp")` 限定搜索范围。

### Q: GLFW / GLM 找不到

**原因**: CMake 默认不搜索 MSYS2 的 Unix 风格路径。  
**解决**: 
- 运行 `scripts\fix_msys2_glfw.bat` 诊断
- 确保通过 `pacman` 安装了正确的子系统版本（ucrt64 / mingw64）
- 运行 `scripts\setup_env.ps1` 一键安装

### Q: `vulkan-1.lib` 链接错误

**原因**: Vulkan SDK 提供的 `vulkan-1.lib` 是 MSVC COFF 格式，MinGW 的 GNU ld 无法解析。  
**解决**: CMake 配置阶段会自动生成 MinGW 兼容的 `libvulkan-1.dll.a`。  
也可手动运行 `scripts\generate_vulkan_mingw_lib.bat`。

### Q: PowerShell 找不到新安装的命令

**解决**: 关闭当前终端并重新打开，或运行：

```powershell
$env:PATH = [Environment]::GetEnvironmentVariable("PATH", "User")
```

---

## 许可证

本项目基于 [MIT 许可证](../LICENSE)。
