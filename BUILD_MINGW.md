快速使用说明 (MinGW + Vulkan SDK)

先决条件:
- 安装 MinGW-w64（确保 g++ / mingw32-make 在 PATH 中）
- Vulkan SDK 已安装在: D:\Program Files\VulkanSDK\1.4.341.1

构建:
1. 打开 "x64 本机工具命令提示符" 或普通 cmd（确保 MinGW 在 PATH）。
2. 在仓库根目录运行: build_mingw.bat

手动 CMake 命令示例:
cmake -G "MinGW Makefiles" -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAN_SDK="D:/Program Files/VulkanSDK/1.4.341.1"
cmake --build build -- -j4

注意:
- glfw-3.4 和 glm 目录假定已在仓库根目录。
- 若使用不同的 Vulkan SDK 路径或 MinGW 编译器，可通过 -DVULKAN_SDK 或 -DCMAKE_C_COMPILER/ -DCMAKE_CXX_COMPILER 覆盖。

## .bat 脚本用途说明

### 构建脚本

| 文件名 | 用途 |
|--------|------|
| `build_mingw.bat` | **主构建脚本**：使用 MinGW Makefiles 编译 Vulkan 项目（Release 模式）。设置 Vulkan SDK 路径，创建 build 目录，运行 CMake 配置并并行构建。 |
| `build_cpp_mingw.bat` | **C++ 简易编译脚本**：直接使用 g++ 编译 simple_app.cpp，无需 CMake。编译后自动运行生成的可执行文件。 |
| `build_vulkan_mingw.bat` | **调试构建脚本**：使用 MinGW 编译 Vulkan 项目（Debug 模式）。编译完成后自动运行 main.exe，运行 3 秒后自动终止。 |

### 依赖管理脚本

| 文件名 | 用途 |
|--------|------|
| `quick_clone_deps.bat` | **快速克隆依赖**：克隆 GLFW 和 GLM 到 external/ 目录，用于快速开发环境搭建。如果目录已存在则跳过。 |
| `update_and_cleanup_deps.bat` | **更新并清理依赖**：更新 GLFW 和 GLM（尝试作为子模块更新或直接 pull），并删除本地备份目录。 |

### CMake 辅助脚本

| 文件名 | 用途 |
|--------|------|
| `cmake/embed_spv.bat` | **SPIR-V 嵌入包装器**：调用 PowerShell 脚本 embed_spv.ps1，用于将 SPIR-V 着色器文件嵌入到 C++ 代码中。 |
