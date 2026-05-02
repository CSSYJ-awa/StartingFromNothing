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
