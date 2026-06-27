# Vulkan + MinGW-w64：导入库生成指南

## 目录

1. [为什么 MinGW 需要单独的导入库？](#1-为什么-mingw-需要单独的导入库)
2. [自动生成机制（CMake 集成）](#2-自动生成机制cmake-集成)
3. [手动生成（Batch 脚本）](#3-手动生成batch-脚本)
4. [工具链安装方法](#4-工具链安装方法)
5. [备选方案](#5-备选方案)
6. [验证检查清单](#6-验证检查清单)

---

## 1. 为什么 MinGW 需要单独的导入库？

### 根本原因：目标文件格式不兼容

| 编译器 | 导入库格式 | 链接器 | 文件后缀 |
|--------|-----------|--------|---------|
| MSVC (Visual Studio) | COFF | `link.exe` | `.lib` |
| MinGW-w64 (GCC) | PE/COFF (GNU ar) | `ld.exe` (GNU ld) | `.a` / `.dll.a` |

Vulkan SDK 官方仅分发 MSVC 格式的 `vulkan-1.lib`。当 MinGW 的 GNU ld 尝试读取该文件时，会因为无法识别 COFF 目标文件格式而报错：

```
undefined reference to `__imp_vkCreateInstance'
```

### 解决方案

使用 MinGW 工具链中的两个标准工具，**直接读取系统级 DLL**（无需复制）：

```
C:\Windows\System32\vulkan-1.dll ──[gendef]──> vulkan-1.def ──[dlltool]──> libvulkan-1.dll.a
        (显卡驱动安装)                                                      (输出到 %VULKAN_SDK%\Lib\)
```

- **`gendef`**：从系统 DLL 读取导出符号表，生成 `.def`（模块定义文件）
- **`dlltool`**：从 `.def` 文件生成 MinGW 兼容的 `.dll.a` 导入库

### 为什么不复制 DLL 到 SDK 目录？

| 问题 | 说明 |
|------|------|
| **权限问题** | `%VULKAN_SDK%` 通常位于 `C:\Program Files` 或 `C:\VulkanSDK`，复制需要管理员权限 |
| **版本冲突** | SDK 自带的 `vulkan-1.dll` 可能与显卡驱动安装的版本不同，导致运行时异常 |
| **磁盘浪费** | `vulkan-1.dll` 约 1-2 MB，系统中已有一份，无需冗余 |
| **ABI 稳定性** | `C:\Windows\System32\vulkan-1.dll` 由显卡驱动维护，是所有 Vulkan 应用运行时的实际加载目标，ABI 最稳定 |

> **注意**：如果 `System32` 下没有 `vulkan-1.dll`，说明显卡驱动未安装或不支持 Vulkan。请安装最新显卡驱动（NVIDIA/AMD/Intel）或从 [vulkan.lunarg.com](https://vulkan.lunarg.com/) 下载 Vulkan Runtime。

---

## 2. 自动生成机制（CMake 集成）

### 触发条件

`cmake/GenerateVulkanMingwLib.cmake` 满足以下**全部**条件时自动激活：

1. 编译器为 MinGW（`MINGW` 为 `TRUE`）
2. `VULKAN_SDK` 环境变量已设置
3. `C:\Windows\System32\vulkan-1.dll` 存在（系统级 DLL，显卡驱动安装）
4. `${VULKAN_SDK}/Lib/libvulkan-1.dll.a` 尚不存在
4. 系统中能找到 `gendef.exe` 和 `dlltool.exe`

### 执行流程

```
CMake 配置阶段
    │
    ├─ include(cmake/GenerateVulkanMingwLib.cmake)
    │       │
    │       ├─ [检查] 是 MinGW 编译器？
    │       │   └─ 否 → 跳过
    │       │
    │       ├─ [检查] libvulkan-1.dll.a 已存在？
    │       │   └─ 是 → [SKIP] 跳过生成
    │       │
    │       ├─ [检查] VULKAN_SDK 已设置？
    │       │   └─ 否 → [WARNING] 提示手动生成
    │       │
    │       ├─ [检查] C:\Windows\System32\vulkan-1.dll 存在？
    │       │   └─ 否 → [WARNING] 提示安装显卡驱动
    │       │
    │       ├─ [检查] gendef + dlltool 可用？
    │       │   └─ 否 → [WARNING] 提示安装工具链
    │       │
    │       └─ [配置] add_custom_command + add_custom_target
    │               │
    │               ├─ 步骤 1: gendef → vulkan-1.def
    │               └─ 步骤 2: dlltool → libvulkan-1.dll.a
    │
    └─ add_dependencies(${PROJECT_NAME} generate_vulkan_mingw_lib)
            │
            └─ 构建时自动执行上述步骤，确保编译前导入库已就绪
```

### CMakeLists.txt 中的集成方式

```cmake
cmake_minimum_required(VERSION 3.15)
project(VulkanApp LANGUAGES CXX)

# 在 project() 之后、add_executable() 之前
include(cmake/GenerateVulkanMingwLib.cmake)
include(cmake/CheckEnvironment.cmake)

# ... 其他配置 ...

add_executable(${PROJECT_NAME} main.cpp)

# 将导入库生成作为编译前置依赖
if(TARGET generate_vulkan_mingw_lib)
    add_dependencies(${PROJECT_NAME} generate_vulkan_mingw_lib)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    # ...
)
```

---

## 3. 手动生成（Batch 脚本）

如果 CMake 自动生成失败或不想使用 CMake，可直接运行：

```batch
.\scripts\generate_vulkan_mingw_lib.bat
```

### 参数

| 参数 | 说明 |
|------|------|
| 无参数 | 交互模式，完成后暂停等待按键 |
| `--no-pause` / `-q` / `/q` | 静默模式，适用于 CI/CD 自动化 |

### 脚本执行流程

```
[0] 环境预检
    ├─ VULKAN_SDK 已设置？
    ├─ vulkan-1.dll 存在？
    └─ libvulkan-1.dll.a 已存在？（是→跳过）

[1] 工具链检查
    ├─ 搜索 gendef.exe（环境变量→PATH→常见路径）
    └─ 搜索 dlltool.exe（PATH→g++ 同目录）

[2] gendef + dlltool（推荐路径）
    ├─ gendef - vulkan-1.dll > vulkan-1.def
    └─ dlltool -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll

[3] dlltool 直接生成（回退路径，仅在 gendef 缺失时）
    ├─ 方法 1: dlltool -k ...
    └─ 方法 2: 最小 .def + dlltool

[4] 验证
    ├─ 文件存在？
    └─ 大小 >= 10KB？（<10KB 警告）
```

---

## 4. 工具链安装方法

### 方法 A：MSYS2（推荐）

MSYS2 提供了最完整的 MinGW-w64 工具链，包含 `gendef` 和 `dlltool`。

```bash
# 安装 UCRT64 完整工具链（推荐用于 Windows 10+）
pacman -S mingw-w64-ucrt-x86_64-toolchain

# 或仅安装 binutils（包含 gendef 和 dlltool）
pacman -S mingw-w64-ucrt-x86_64-binutils
```

安装后，确保 `C:\msys64\ucrt64\bin` 在系统 PATH 中。

### 方法 B：独立 MinGW-w64

从 [mingw-w64.org](https://www.mingw-w64.org/) 下载安装包，确保选择包含 **binutils** 的完整版本。

推荐发行版：
- [WinLibs](https://winlibs.com/) — 提供含 binutils 的独立 MinGW-w64 构建
- [TDM-GCC](https://jmeubank.github.io/tdm-gcc/) — 轻量级 MinGW 发行版

### 验证安装

```batch
gendef --version
dlltool --version
```

两者均应输出版本信息。

---

## 5. 备选方案

如果无法或不想使用 `gendef` + `dlltool`，有以下替代方案：

### 方案 A：从 MSYS2 仓库提取预编译 .dll.a

MSYS2 包仓库中有预编译的 `libvulkan-1.dll.a`：

```
https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-vulkan-loader
```

下载 `.pkg.tar.zst` 包，用 7-Zip 解压，提取 `ucrt64/lib/libvulkan-1.dll.a` 放入 `%VULKAN_SDK%/Lib/`。

### 方案 B：使用 vcpkg 安装 Vulkan

```powershell
# 安装 vcpkg（如尚未安装）
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 安装面向 MinGW 的 Vulkan
.\vcpkg install vulkan --triplet=x64-mingw-static
```

vcpkg 会自动生成 MinGW 兼容的库文件。

### 方案 C：直接链接 DLL（不推荐）

作为**最后手段**，可以直接链接 `vulkan-1.dll` 而非导入库：

```cmake
# 不推荐：绕过导入库直接链接 DLL
target_link_libraries(${PROJECT_NAME} PRIVATE
    "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll"
)
```

**缺点**：
- 每次构建都要重新解析 DLL
- 某些符号可能无法正确解析
- 不符合 CMake 最佳实践
- 可能产生运行时问题

---

## 6. 验证检查清单

生成完成后，逐项确认：

| # | 检查项 | 命令 / 方法 | 预期结果 |
|---|--------|------------|---------|
| 1 | 文件存在 | `dir "%VULKAN_SDK%\Lib\libvulkan-1.dll.a"` | 找到文件 |
| 2 | 文件大小 | 资源管理器 → 属性 | > 10 KB（通常 30-100 KB） |
| 3 | CMake 识别 | `cmake -S . -B build -G "MinGW Makefiles"` | `Found Vulkan: ...` |
| 4 | 编译链接 | `cmake --build build` | 无 `undefined reference` 错误 |
| 5 | 程序运行 | 双击 exe 或命令行运行 | 正常启动，无 DLL 缺失错误 |
| 6 | 符号验证 | `nm "%VULKAN_SDK%\Lib\libvulkan-1.dll.a" \| findstr vkCreateInstance` | 显示 `T __imp_vkCreateInstance` 等符号 |

### 快速验证命令

```batch
:: 检查是否存在且大小合理
dir "%VULKAN_SDK%\Lib\libvulkan-1.dll.a"

:: 检查符号是否正常（需要 MinGW 的 nm 工具）
nm "%VULKAN_SDK%\Lib\libvulkan-1.dll.a" | findstr "vkCreate"

:: 运行环境检测脚本
.\scripts\check_env.bat --no-pause
```

---

## 常见问题

### Q: 生成后仍然报 `undefined reference`？

A: 确保 CMakeLists.txt 中链接顺序正确：
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan    # 必须放在最前面
    glfw
    gdi32
    opengl32
    m
)
```

### Q: `nm` 命令找不到？

A: `nm` 位于 MinGW 的 `bin` 目录。如果 PATH 中有 `g++`，通常也在同一目录下：
```batch
where g++
dir "同目录\nm.exe"
```

### Q: 多个 MinGW 版本冲突？

A: 确保 PATH 中只有一个 MinGW bin 目录，或使用绝对路径指定工具。
