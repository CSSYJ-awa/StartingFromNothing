# ==============================================================================
# GenerateVulkanMingwLib.cmake
#
# 调用方式：在 CMakeLists.txt 中 project() 之后添加
#   include(cmake/GenerateVulkanMingwLib.cmake)
#
# 功能：当使用 MinGW 编译器时，自动从 vulkan-1.dll 生成 MinGW 兼容的
#       导入库 libvulkan-1.dll.a，并作为构建前置依赖。
#
# 背景：
#   Vulkan SDK 官方仅提供 MSVC 格式的 vulkan-1.lib（COFF 格式），
#   而 MinGW 的 GNU ld 链接器需要 PE/COFF 格式的 .dll.a 导入库。
#   gendef 从系统 DLL (C:/Windows/System32/vulkan-1.dll) 提取导出符号
#   生成 .def 文件，dlltool 再将 .def 转换为 .dll.a 导入库。
#
#   为什么不使用 SDK Bin 下的 DLL？
#     · 系统 DLL 由显卡驱动安装，是运行时实际加载的版本，ABI 最稳定
#     · 无需复制文件，避免权限问题和磁盘浪费
#     · 避免 SDK DLL 与驱动 DLL 版本不一致导致的运行时问题
#
# 触发条件：
#   1. 编译器为 MinGW (MINGW)
#   2. VULKAN_SDK 环境变量已设置
#   3. libvulkan-1.dll.a 尚不存在
#   4. 找到了 gendef 和 dlltool
# ==============================================================================

# ---- 仅在 MinGW 环境下生效 ----
if(NOT MINGW)
    message(STATUS "[VULKAN] 非 MinGW 编译器，跳过导入库生成。")
    return()
endif()

# ---- 检查 VULKAN_SDK ----
if(NOT DEFINED ENV{VULKAN_SDK})
    message(WARNING "[VULKAN] VULKAN_SDK 未设置，无法自动生成 MinGW 导入库。"
                    "请手动运行 scripts/generate_vulkan_mingw_lib.bat。")
    return()
endif()

# ---- 系统级 vulkan-1.dll（显卡驱动安装，ABI 稳定）----
set(VULKAN_DLL "C:/Windows/System32/vulkan-1.dll")
set(VULKAN_LIB_DIR "$ENV{VULKAN_SDK}/Lib")
set(VULKAN_MINGW_LIB "${VULKAN_LIB_DIR}/libvulkan-1.dll.a")

# ---- 如果导入库已存在，跳过生成 ----
if(EXISTS "${VULKAN_MINGW_LIB}")
    file(SIZE "${VULKAN_MINGW_LIB}" VULKAN_MINGW_LIB_SIZE)
    message(STATUS "[VULKAN] MinGW 导入库已就绪 (${VULKAN_MINGW_LIB_SIZE} 字节) → [SKIP]")
    return()
endif()

# ---- 验证系统 vulkan-1.dll 存在 ----
if(NOT EXISTS "${VULKAN_DLL}")
    message(WARNING "[VULKAN] 未找到系统 Vulkan DLL (${VULKAN_DLL})。\n"
                    "  该文件由显卡驱动或 Vulkan Runtime 安装。请：\n"
                    "  1. 安装/更新显卡驱动 (NVIDIA/AMD/Intel)\n"
                    "  2. 或安装 Vulkan Runtime: https://vulkan.lunarg.com/\n"
                    "  无法自动生成导入库。")
    return()
endif()

# ---- 查找工具 ----
find_program(GENDEF_EXE
    NAMES gendef
    PATHS
        "$ENV{MINGW_PREFIX}/bin"
        "$ENV{MSYSTEM_PREFIX}/bin"
        "C:/msys64/ucrt64/bin"
        "C:/msys64/mingw64/bin"
        "C:/msys64/clang64/bin"
        "C:/mingw64/bin"
        "C:/MinGW/bin"
        "C:/TDM-GCC-64/bin"
    DOC "gendef - MinGW symbol export tool"
)

find_program(DLLTOOL_EXE
    NAMES dlltool
    PATHS
        "$ENV{MINGW_PREFIX}/bin"
        "$ENV{MSYSTEM_PREFIX}/bin"
        "C:/msys64/ucrt64/bin"
        "C:/msys64/mingw64/bin"
        "C:/msys64/clang64/bin"
        "C:/mingw64/bin"
        "C:/MinGW/bin"
        "C:/TDM-GCC-64/bin"
    DOC "dlltool - MinGW import library generator"
)

# ---- 工具未找到的情况 ----
if(NOT GENDEF_EXE OR NOT DLLTOOL_EXE)
    set(MISSING_TOOLS "")
    if(NOT GENDEF_EXE)
        set(MISSING_TOOLS "${MISSING_TOOLS}  gendef")
    endif()
    if(NOT DLLTOOL_EXE)
        set(MISSING_TOOLS "${MISSING_TOOLS}  dlltool")
    endif()

    message(WARNING "[VULKAN] 缺少 MinGW 工具链组件，无法自动生成导入库。"
                    "缺少:${MISSING_TOOLS}\n"
                    "请通过以下方式之一获取：\n"
                    "  1. MSYS2: pacman -S mingw-w64-ucrt-x86_64-binutils\n"
                    "  2. 手动运行: scripts\\generate_vulkan_mingw_lib.bat\n"
                    "  3. vcpkg: vcpkg install vulkan --triplet=x64-mingw-static")
    return()
endif()

# ============================================================================
# 创建生成任务
# ============================================================================
message(STATUS "[VULKAN] 工具链就绪，配置自动生成 MinGW 导入库...")
message(STATUS "         gendef  : ${GENDEF_EXE}")
message(STATUS "         dlltool : ${DLLTOOL_EXE}")
message(STATUS "         源 DLL  : ${VULKAN_DLL}")
message(STATUS "         输出    : ${VULKAN_MINGW_LIB}")

set(VULKAN_DEF_FILE "${VULKAN_LIB_DIR}/vulkan-1.def")

# ---- 步骤 1: gendef 导出符号定义 ----
add_custom_command(
    OUTPUT "${VULKAN_DEF_FILE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_LIB_DIR}"
    COMMAND "${GENDEF_EXE}" - "${VULKAN_DLL}" > "${VULKAN_DEF_FILE}"
    DEPENDS "${VULKAN_DLL}"
    COMMENT "[VULKAN] 步骤 1/2: gendef 导出符号定义 → vulkan-1.def"
    WORKING_DIRECTORY "${VULKAN_LIB_DIR}"
    VERBATIM
)

# ---- 步骤 2: dlltool 生成导入库 ----
add_custom_command(
    OUTPUT "${VULKAN_MINGW_LIB}"
    COMMAND "${DLLTOOL_EXE}"
        -d "${VULKAN_DEF_FILE}"
        -l "${VULKAN_MINGW_LIB}"
        -D "${VULKAN_DLL}"
    DEPENDS "${VULKAN_DEF_FILE}"
    COMMENT "[VULKAN] 步骤 2/2: dlltool 生成导入库 → libvulkan-1.dll.a"
    WORKING_DIRECTORY "${VULKAN_LIB_DIR}"
    VERBATIM
)

# ---- 创建自定义目标 ----
add_custom_target(generate_vulkan_mingw_lib
    DEPENDS "${VULKAN_MINGW_LIB}"
    COMMENT "Generating MinGW import library for Vulkan"
)

# ---- 将生成目标设为项目编译的前置依赖 ----
# 在 CMakeLists.txt 中调用本模块后，执行 add_dependencies
# 此变量供 CMakeLists.txt 引用
set(VULKAN_MINGW_LIB_TARGET "generate_vulkan_mingw_lib" CACHE INTERNAL
    "Custom target for MinGW Vulkan import library generation")

message(STATUS "[VULKAN] 导入库自动生成已配置 → [OK]")
message(STATUS "         源 DLL  : ${VULKAN_DLL} (系统级, 无需复制)")
message(STATUS "         输出    : ${VULKAN_MINGW_LIB}")
