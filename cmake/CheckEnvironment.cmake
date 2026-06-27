# ==============================================================================
# CheckEnvironment.cmake —— 开发环境完整性诊断模块
#
# 调用方式：在 CMakeLists.txt 中 project() 之后添加
#   include(cmake/CheckEnvironment.cmake)
#
# 检测项：
#   1. CMake 版本        >= 3.15
#   2. C++ 编译器         GNU (MinGW) 且版本 >= 8.0
#   3. Vulkan SDK         环境变量 + 头文件 + 导入库
#   4. GLFW               库文件 + 头文件
#   5. GLM                纯头文件库
#
# 【设计说明】
#   · 环境变量 VULKAN_SDK 必须指向 Vulkan SDK 安装根目录。
#     运行时 vulkan-1.dll 位于 %VULKAN_SDK%\Bin\ 下 —— CMake 的
#     find_package(Vulkan) 通过该环境变量自动定位 Include/ 和 Lib/。
#   · 检测 GLFW 时 find_package 可能失败的原因：
#     a) MinGW 下 GLFW 的 CMake 配置文件可能被命名为 glfw3Config.cmake
#        或 glfwConfig.cmake，取决于打包方式；
#     b) GLFW 可能未注册到 CMake 的模块路径（CMAKE_PREFIX_PATH）；
#     c) 用户可能只拷贝了 .a 和 .h 文件而未安装完整的 CMake 配置。
#     因此本模块同时尝试 glfw3 和 glfw 两个包名，并在失败时给出
#     手动设置 GLFW_ROOT 的指引。
# ==============================================================================

# ---- 辅助宏：打印带颜色的状态行 ----
# 注意：Windows 路径含反斜杠，直接放入 message() 会导致 CMake 尝试
# 解析 \P、\V 等转义序列而报错。因此调用者必须先将路径中的 \ 替换为 /。
macro(check_print_status label result detail)
    if(${result})
        message(STATUS "  ${label}  [OK]    ${detail}")
    else()
        message(STATUS "  ${label}  [FAIL]  ${detail}")
    endif()
endmacro()

# ---- 辅助宏：打印分隔线 ----
macro(check_print_separator)
    message(STATUS "  --------------------------------------------------")
endmacro()

# ---- 计数变量 ----
set(CHECK_PASS_COUNT 0)
set(CHECK_FAIL_COUNT 0)

macro(check_record result)
    if(${result})
        math(EXPR CHECK_PASS_COUNT "${CHECK_PASS_COUNT} + 1")
    else()
        math(EXPR CHECK_FAIL_COUNT "${CHECK_FAIL_COUNT} + 1")
    endif()
endmacro()

# ============================================================================
# 阶段 0：CMake 自身版本
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════╗")
message(STATUS "║       VulkanApp 开发环境诊断 (CMake)                ║")
message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "")

check_print_separator()
message(STATUS "  [0] CMake 版本")
check_print_separator()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
    check_print_status("CMake 版本 >= 3.15" TRUE "CMake ${CMAKE_VERSION}")
    check_record(TRUE)
else()
    check_print_status("CMake 版本 >= 3.15" FALSE "当前 ${CMAKE_VERSION}，请升级至 3.15+")
    check_record(FALSE)
    message(FATAL_ERROR "CMake 版本过低 (${CMAKE_VERSION})，需要 >= 3.15。"
                        "请从 https://cmake.org/download/ 下载最新版本。")
endif()

# ============================================================================
# 阶段 1：C++ 编译器
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [1] C++ 编译器")
check_print_separator()

set(COMPILER_OK TRUE)

# --- 编译器 ID ---
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    check_print_status("编译器类型 (GNU)" TRUE "${CMAKE_CXX_COMPILER_ID}")
else()
    check_print_status("编译器类型 (GNU)" FALSE
        "当前: ${CMAKE_CXX_COMPILER_ID}。强烈建议使用 MinGW-w64 (GNU)。")
    set(COMPILER_OK FALSE)
endif()

# --- 编译器版本 ---
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
    check_print_status("编译器版本 >= 8.0" TRUE "GCC ${CMAKE_CXX_COMPILER_VERSION}")
else()
    check_print_status("编译器版本 >= 8.0" FALSE
        "当前 GCC ${CMAKE_CXX_COMPILER_VERSION}，C++17 需要 >= 8.0")
    set(COMPILER_OK FALSE)
endif()

# --- 编译器路径 ---
check_print_status("编译器路径" TRUE "${CMAKE_CXX_COMPILER}")

check_record(${COMPILER_OK})
if(NOT COMPILER_OK)
    message(FATAL_ERROR "编译器不满足要求。请安装 MinGW-w64 (GCC >= 8.0)：\n"
                        "  https://www.mingw-w64.org/\n"
                        "  或 TDM-GCC: https://jmeubank.github.io/tdm-gcc/")
endif()

# ============================================================================
# 阶段 2：Vulkan SDK
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [2] Vulkan SDK")
check_print_separator()

set(VULKAN_OK TRUE)

# 将环境变量转换为 CMake 正斜杠路径（Windows 反斜杠会导致 CMake 转义序列错误）
set(VULKAN_SDK_ROOT "$ENV{VULKAN_SDK}")
if(VULKAN_SDK_ROOT)
    file(TO_CMAKE_PATH "${VULKAN_SDK_ROOT}" VULKAN_SDK_ROOT)
endif()

# --- 环境变量 ---
if(VULKAN_SDK_ROOT)
    check_print_status("VULKAN_SDK 环境变量" TRUE "${VULKAN_SDK_ROOT}")
else()
    check_print_status("VULKAN_SDK 环境变量" FALSE
        "未设置。请从 https://vulkan.lunarg.com/ 下载安装 Vulkan SDK")
    set(VULKAN_OK FALSE)
endif()

# --- 头文件 ---
if(VULKAN_SDK_ROOT)
    set(VULKAN_HEADER "${VULKAN_SDK_ROOT}/Include/vulkan/vulkan.h")
    if(EXISTS "${VULKAN_HEADER}")
        check_print_status("vulkan/vulkan.h 头文件" TRUE "${VULKAN_HEADER}")
    else()
        check_print_status("vulkan/vulkan.h 头文件" FALSE
            "缺少: ${VULKAN_HEADER}")
        set(VULKAN_OK FALSE)
    endif()
endif()

# --- 导入库 (libvulkan-1.dll.a 或 libvulkan.a) ---
if(VULKAN_SDK_ROOT)
    # MinGW 通常使用 libvulkan-1.dll.a（gendef + dlltool 生成）
    # MSVC 使用 vulkan-1.lib（COFF 格式，MinGW 无法直接链接）
    set(VULKAN_LIB_MINGW "${VULKAN_SDK_ROOT}/Lib/libvulkan-1.dll.a")
    set(VULKAN_LIB_MSVC   "${VULKAN_SDK_ROOT}/Lib/vulkan-1.lib")
    set(VULKAN_LIB_STATIC "${VULKAN_SDK_ROOT}/Lib/libvulkan.a")

    if(EXISTS "${VULKAN_LIB_MINGW}")
        file(SIZE "${VULKAN_LIB_MINGW}" VULKAN_LIB_MINGW_SIZE)
        check_print_status("Vulkan 导入库 (MinGW)" TRUE
            "${VULKAN_LIB_MINGW} (${VULKAN_LIB_MINGW_SIZE} bytes)")
    elseif(EXISTS "${VULKAN_LIB_STATIC}")
        check_print_status("Vulkan 静态库" TRUE "${VULKAN_LIB_STATIC}")
    elseif(EXISTS "${VULKAN_LIB_MSVC}" AND MINGW)
        # MinGW 环境下只有 MSVC .lib 不可用，但这不是致命错误（可自动生成）
        check_print_status("Vulkan 导入库 (MinGW)" FALSE
            "仅有 MSVC 格式 vulkan-1.lib，MinGW 无法链接")
        message(STATUS "                                            → 运行 scripts\\generate_vulkan_mingw_lib.bat 生成")
        message(STATUS "                                            → 或 CMake 将自动生成（若工具链完整）")
        # 不将 VULKAN_OK 设为 FALSE——因为 CMake 的 GenerateVulkanMingwLib 会自动处理
    elseif(EXISTS "${VULKAN_LIB_MSVC}" AND NOT MINGW)
        check_print_status("Vulkan 导入库 (MSVC)" TRUE "${VULKAN_LIB_MSVC}")
    else()
        check_print_status("Vulkan 导入库" FALSE
            "缺少: ${VULKAN_LIB_MINGW}\n"
            "                                           请确认 Vulkan SDK 安装完整")
        set(VULKAN_OK FALSE)
    endif()
endif()

# --- MinGW 工具链（gendef / dlltool）---
if(MINGW AND VULKAN_SDK_ROOT AND NOT EXISTS "${VULKAN_LIB_MINGW}")
    message(STATUS "")
    check_print_separator()
    message(STATUS "  [2.1] MinGW 导入库生成工具")
    check_print_separator()

    find_program(CHK_GENDEF gendef)
    find_program(CHK_DLLTOOL dlltool)

    if(CHK_GENDEF)
        check_print_status("gendef" TRUE "${CHK_GENDEF}")
    else()
        check_print_status("gendef" FALSE "未找到，导入库自动生成将不可用")
    endif()

    if(CHK_DLLTOOL)
        check_print_status("dlltool" TRUE "${CHK_DLLTOOL}")
    else()
        check_print_status("dlltool" FALSE "未找到，导入库自动生成将不可用")
    endif()

    if(NOT CHK_GENDEF OR NOT CHK_DLLTOOL)
        message(STATUS "                                            修复: MSYS2 中运行")
        message(STATUS "                                              pacman -S mingw-w64-ucrt-x86_64-binutils")
        message(STATUS "                                            或手动运行 scripts\\generate_vulkan_mingw_lib.bat")
    endif()
endif()

# --- 运行时 DLL (系统级, C:\Windows\System32) ---
#     该 DLL 由显卡驱动或 Vulkan Runtime 安装，是运行时实际加载的版本。
#     不使用 SDK Bin 下的副本 —— 避免版本冲突、权限问题和磁盘浪费。
set(VULKAN_SYSTEM_DLL "C:/Windows/System32/vulkan-1.dll")
if(EXISTS "${VULKAN_SYSTEM_DLL}")
    check_print_status("vulkan-1.dll (System32)" TRUE "${VULKAN_SYSTEM_DLL}")
else()
    check_print_status("vulkan-1.dll (System32)" FALSE
        "缺少: ${VULKAN_SYSTEM_DLL}\n"
        "                                           请安装显卡驱动或 Vulkan Runtime")
    set(VULKAN_OK FALSE)
endif()

check_record(${VULKAN_OK})
if(NOT VULKAN_OK)
    message(FATAL_ERROR "Vulkan SDK 检测失败。\n"
                        "  1. 下载 Vulkan SDK: https://vulkan.lunarg.com/\n"
                        "  2. 安装后确保 VULKAN_SDK 环境变量已设置（安装程序通常自动设置）\n"
                        "  3. 如手动设置，请指向 SDK 根目录，例如 C:\\VulkanSDK\\1.3.296.0")
endif()

# ============================================================================
# 阶段 3：GLFW
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [3] GLFW 窗口库")
check_print_separator()

set(GLFW_OK TRUE)

# --- 先尝试 glfw3，再尝试 glfw ---
# 原因：不同打包方式的 CMake 配置文件名可能不同
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    check_print_status("find_package(glfw3)" TRUE "已找到")
    if(DEFINED glfw3_VERSION)
        check_print_status("GLFW 版本" TRUE "${glfw3_VERSION}")
    endif()
else()
    find_package(glfw QUIET)
    if(glfw_FOUND)
        check_print_status("find_package(glfw)" TRUE "已找到 (包名: glfw)")
        if(DEFINED glfw_VERSION)
            check_print_status("GLFW 版本" TRUE "${glfw_VERSION}")
        endif()
    else()
        check_print_status("find_package(glfw3/glfw)" FALSE
            "CMake 未找到 GLFW 配置文件")

        # 尝试手动查找库文件
        # 先推断 MSYS2 可能的安装路径
        set(GLFW_MSYS2_PATHS "")
        get_filename_component(_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_SUBSYSTEM_DIR "${_COMPILER_DIR}" DIRECTORY)
        get_filename_component(_MSYS2_MAYBE_ROOT "${_SUBSYSTEM_DIR}" DIRECTORY)
        foreach(_SUB mingw64 ucrt64 clang64 mingw32 clang32)
            if(IS_DIRECTORY "${_MSYS2_MAYBE_ROOT}/${_SUB}")
                list(APPEND GLFW_MSYS2_PATHS "${_MSYS2_MAYBE_ROOT}/${_SUB}/lib")
            endif()
        endforeach()
        # 也尝试常见 MSYS2 根目录
        foreach(_ROOT C:/msys64 C:/msys2 D:/msys64 D:/msys2 "D:/Program Files/msys64")
            if(IS_DIRECTORY "${_ROOT}")
                foreach(_SUB mingw64 ucrt64 clang64 mingw32 clang32)
                    if(IS_DIRECTORY "${_ROOT}/${_SUB}")
                        list(APPEND GLFW_MSYS2_PATHS "${_ROOT}/${_SUB}/lib")
                    endif()
                endforeach()
            endif()
        endforeach()

        find_library(GLFW_LIB_MANUAL
            NAMES glfw3 glfw3dll glfw
            PATHS
                "$ENV{GLFW_ROOT}/lib-mingw-w64"
                "$ENV{GLFW_ROOT}/lib"
                "$ENV{GLFW_DIR}/lib-mingw-w64"
                "$ENV{GLFW_DIR}/lib"
                "C:/glfw/lib-mingw-w64"
                "C:/glfw/lib"
                ${GLFW_MSYS2_PATHS}
        )

        # 推断 MSYS2 的头文件路径
        set(GLFW_INC_MSYS2_PATHS "")
        foreach(_LIB_PATH ${GLFW_MSYS2_PATHS})
            string(REPLACE "/lib" "/include" _INC_PATH "${_LIB_PATH}")
            list(APPEND GLFW_INC_MSYS2_PATHS "${_INC_PATH}")
        endforeach()

        find_path(GLFW_INC_MANUAL
            NAMES GLFW/glfw3.h
            PATHS
                "$ENV{GLFW_ROOT}/include"
                "$ENV{GLFW_DIR}/include"
                "C:/glfw/include"
                ${GLFW_INC_MSYS2_PATHS}
        )

        if(GLFW_LIB_MANUAL)
            check_print_status("GLFW 库文件 (手动查找)" TRUE "${GLFW_LIB_MANUAL}")
        else()
            check_print_status("GLFW 库文件 (手动查找)" FALSE "未找到 glfw3.a / glfw3.dll.a")
            set(GLFW_OK FALSE)
        endif()

        if(GLFW_INC_MANUAL)
            check_print_status("GLFW 头文件 (手动查找)" TRUE "${GLFW_INC_MANUAL}")
        else()
            check_print_status("GLFW 头文件 (手动查找)" FALSE "未找到 GLFW/glfw3.h")
            set(GLFW_OK FALSE)
        endif()
    endif()
endif()

check_record(${GLFW_OK})
if(NOT GLFW_OK)
    message(WARNING "GLFW not found. Solutions:\n"
                    "  A) Set GLFW_ROOT to GLFW install dir\n"
                    "     e.g. set GLFW_ROOT=C:\\glfw\n"
                    "  B) Install via MSYS2: pacman -S mingw-w64-x86_64-glfw\n"
                    "  C) Download from https://www.glfw.org/download.html")
endif()

# ============================================================================
# 阶段 4：GLM 数学库
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [4] GLM 数学库 (纯头文件)")
check_print_separator()

set(GLM_OK TRUE)

find_package(glm QUIET)
if(glm_FOUND)
    check_print_status("find_package(glm)" TRUE "已找到")
    if(DEFINED GLM_INCLUDE_DIRS)
        check_print_status("GLM 包含路径" TRUE "${GLM_INCLUDE_DIRS}")
    endif()
else()
    # 手动搜索头文件
    # 先推断 MSYS2 可能的安装路径
    set(GLM_MSYS2_INC_PATHS "")
    get_filename_component(_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_SUBSYSTEM_DIR "${_COMPILER_DIR}" DIRECTORY)
    get_filename_component(_MSYS2_MAYBE_ROOT "${_SUBSYSTEM_DIR}" DIRECTORY)
    foreach(_SUB mingw64 ucrt64 clang64 mingw32 clang32)
        if(IS_DIRECTORY "${_MSYS2_MAYBE_ROOT}/${_SUB}/include")
            list(APPEND GLM_MSYS2_INC_PATHS "${_MSYS2_MAYBE_ROOT}/${_SUB}/include")
        endif()
    endforeach()
    # 也尝试常见 MSYS2 根目录
    foreach(_ROOT C:/msys64 C:/msys2 D:/msys64 D:/msys2 "D:/Program Files/msys64")
        if(IS_DIRECTORY "${_ROOT}")
            foreach(_SUB mingw64 ucrt64 clang64 mingw32 clang32)
                if(IS_DIRECTORY "${_ROOT}/${_SUB}/include")
                    list(APPEND GLM_MSYS2_INC_PATHS "${_ROOT}/${_SUB}/include")
                endif()
            endforeach()
        endif()
    endforeach()

    find_path(GLM_INC_MANUAL
        NAMES glm/glm.hpp
        PATHS
            "$ENV{GLM_ROOT}/include"
            "$ENV{GLM_DIR}"
            "$ENV{GLM_INCLUDE_DIR}"
            "C:/glm"
            "${CMAKE_SOURCE_DIR}/include"
            ${GLM_MSYS2_INC_PATHS}
    )

    if(GLM_INC_MANUAL)
        check_print_status("GLM 头文件 (手动查找)" TRUE "${GLM_INC_MANUAL}")
        # 将手动找到的路径设置为 GLM_INCLUDE_DIRS 供后续使用
        set(GLM_INCLUDE_DIRS "${GLM_INC_MANUAL}" CACHE PATH "GLM include directory")
    else()
        check_print_status("GLM 头文件" FALSE "未找到 glm/glm.hpp")
        set(GLM_OK FALSE)
    endif()
endif()

check_record(${GLM_OK})
if(NOT GLM_OK)
    message(WARNING "GLM not found. GLM is header-only, no compilation needed.\n"
                    "  A) MSYS2: pacman -S mingw-w64-x86_64-glm\n"
                    "  B) Download from https://github.com/g-truc/glm/releases\n"
                    "     Extract and set GLM_ROOT or GLM_DIR to the extracted dir\n"
                    "  C) Copy glm folder into ${CMAKE_SOURCE_DIR}/include/")
endif()

# ============================================================================
# 汇总报告
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════╗")
message(STATUS "║              环境诊断结果汇总                        ║")
message(STATUS "╠══════════════════════════════════════════════════════╣")

math(EXPR CHECK_TOTAL "${CHECK_PASS_COUNT} + ${CHECK_FAIL_COUNT}")
message(STATUS "║  总计检测 ${CHECK_TOTAL} 项")
message(STATUS "║  通过: ${CHECK_PASS_COUNT}  [OK]")

if(CHECK_FAIL_COUNT GREATER 0)
    message(STATUS "║  失败: ${CHECK_FAIL_COUNT}  [FAIL]  ← 请根据上述建议修复")
    message(STATUS "╠══════════════════════════════════════════════════════╣")
    message(STATUS "║  部分检测未通过，构建可能失败。                      ║")
    message(STATUS "║  Follow the [FAIL] suggestions above to fix.           ║")
else()
    message(STATUS "║  失败: 0")
    message(STATUS "╠══════════════════════════════════════════════════════╣")
    message(STATUS "║  所有检测通过！环境就绪，可以构建。                  ║")
endif()

message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "")

# ---- 清理内部变量，避免污染全局命名空间 ----
# (保留 GLM_INCLUDE_DIRS 供后续使用)
