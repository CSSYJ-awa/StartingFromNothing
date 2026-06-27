/**
 * VulkanApp —— 入口
 *
 * 创建 VulkanApp 实例并进入主循环。
 * 应用逻辑封装在 vulkan_app.h / vulkan_app.cpp 中。
 */
#include "vulkan_app.h"

#include <iostream>
#include <stdexcept>
#include <clocale>

// ============================================================================
// Windows 控制台 UTF-8 支持
//
// Windows 控制台默认使用 GBK (代码页 936) 编码，而 VulkanApp 内部使用
// UTF-8 字符串（C++ 源码文件也是 UTF-8 编码）。直接输出中文会显示为乱码
// （如 "瑕佹眰" 应为 "要求"）。
//
// 解决方案：
//   1. SetConsoleOutputCP(CP_UTF8)  → 将输出代码页切换为 UTF-8 (65001)
//   2. SetConsoleCP(CP_UTF8)        → 将输入代码页也切换为 UTF-8
//   3. std::setlocale(LC_ALL, ".UTF-8") → 让 C/C++ 标准库函数也使用 UTF-8
//
// 这些设置仅在 Windows 上需要，Linux/macOS 终端原生支持 UTF-8。
// 通过条件编译宏 #ifdef _WIN32 确保跨平台兼容。
// ============================================================================
#ifdef _WIN32
#include <windows.h>
#endif

static void setupConsoleEncoding()
{
#ifdef _WIN32
    // 设置 C 标准库区域为 UTF-8，使 multibyte <-> wide char 转换正确
    std::setlocale(LC_ALL, ".UTF-8");

    // 将控制台输出代码页设为 UTF-8 (65001)
    // 所有 std::cout / std::cerr / printf 输出的 UTF-8 中文将正确显示
    if (SetConsoleOutputCP(CP_UTF8) == 0)
    {
        // 失败时静默继续，不影响程序正常运行
        DWORD err = GetLastError();
        std::cerr << "[警告] SetConsoleOutputCP(CP_UTF8) 失败，错误码: " << err << std::endl;
    }

    // 可选：同时设置输入代码页，使 std::cin 也能读取 UTF-8 输入
    if (SetConsoleCP(CP_UTF8) == 0)
    {
        DWORD err = GetLastError();
        std::cerr << "[警告] SetConsoleCP(CP_UTF8) 失败，错误码: " << err << std::endl;
    }
#else
    // Linux/macOS: 设置 UTF-8 locale，确保 wide char 函数正常工作
    std::setlocale(LC_ALL, "en_US.UTF-8");
#endif
}

int main()
{
    // ---- 在所有输出之前设置控制台编码 ----
    setupConsoleEncoding();

    try
    {
        VulkanApp app;
        app.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[错误] " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[VulkanApp] 程序正常退出。" << std::endl;
    return EXIT_SUCCESS;
}
