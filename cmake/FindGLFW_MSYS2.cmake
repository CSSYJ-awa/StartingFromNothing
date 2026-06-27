# ==============================================================================
# FindGLFW_MSYS2.cmake
#
# 用途：当标准 find_package(glfw3) 失败时，自动在 MSYS2 安装路径下搜索 GLFW。
#
# 背景：
#   MSYS2 将 GLFW 安装在 /mingw64/（或 /ucrt64/、/clang64/）下，
#   但从 Windows 原生命令行启动的 CMake 不会搜索这些 Unix 风格路径。
#   本模块自动推断 MSYS2 根目录，并设置 cmake 变量使后续构建正常进行。
#
# 调用方式：在 find_package(glfw3 QUIET) 失败后 include：
#   if(NOT glfw3_FOUND)
#       include(cmake/FindGLFW_MSYS2.cmake)
#   endif()
#
# 输出变量（与 find_package(glfw3) 保持兼容）：
#   GLFW_FOUND          - TRUE 或 FALSE
#   GLFW_INCLUDE_DIRS   - 头文件路径
#   GLFW_LIBRARIES      - 库文件完整路径
#   GLFW_LIBRARY        - 同 GLFW_LIBRARIES（兼容旧变量名）
# ==============================================================================

# ---- 仅在 MinGW 环境下激活 ----
if(NOT MINGW)
    message(STATUS "[GLFW MSYS2] 非 MinGW 编译器，跳过 MSYS2 检测。")
    return()
endif()

# ============================================================================
# 阶段 1：推断 MSYS2 根目录
# ============================================================================

set(MSYS2_ROOT_DIR "")

# ---- 1.1 从环境变量 MSYS2_ROOT 读取（用户可手动设置）----
if(DEFINED ENV{MSYS2_ROOT})
    file(TO_CMAKE_PATH "$ENV{MSYS2_ROOT}" MSYS2_ROOT_DIR)
    message(STATUS "[GLFW MSYS2] 从 MSYS2_ROOT 环境变量获取: ${MSYS2_ROOT_DIR}")
endif()

# ---- 1.2 从编译器路径反推 ----
# 例如 g++ 位于 C:/msys64/ucrt64/bin/g++.exe → MSYS2 根目录 = C:/msys64
if(NOT MSYS2_ROOT_DIR)
    get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    # 编译器通常在 {root}/{subsystem}/bin/ 下，回退两级
    get_filename_component(SUBSYSTEM_DIR "${COMPILER_DIR}" DIRECTORY)
    get_filename_component(MAYBE_ROOT  "${SUBSYSTEM_DIR}"  DIRECTORY)

    # 验证该目录下是否有 mingw64 或 ucrt64 等子目录（典型的 MSYS2 结构）
    set(MSYS2_SIGNATURE_DIRS "mingw64;ucrt64;clang64;mingw32;clang32")
    set(FOUND_SIGNATURE FALSE)
    foreach(_SUB ${MSYS2_SIGNATURE_DIRS})
        if(IS_DIRECTORY "${MAYBE_ROOT}/${_SUB}")
            set(FOUND_SIGNATURE TRUE)
            break()
        endif()
    endforeach()

    if(FOUND_SIGNATURE)
        set(MSYS2_ROOT_DIR "${MAYBE_ROOT}")
        message(STATUS "[GLFW MSYS2] 从编译器路径反推: ${MSYS2_ROOT_DIR}")
    endif()
endif()

# ---- 1.3 尝试常见的 MSYS2 安装路径 ----
if(NOT MSYS2_ROOT_DIR)
    set(MSYS2_CANDIDATE_PATHS
        "C:/msys64"
        "C:/msys2"
        "D:/msys64"
        "D:/msys2"
        "E:/msys64"
        "E:/msys2"
    )
    foreach(_CANDIDATE ${MSYS2_CANDIDATE_PATHS})
        if(IS_DIRECTORY "${_CANDIDATE}")
            set(MSYS2_ROOT_DIR "${_CANDIDATE}")
            message(STATUS "[GLFW MSYS2] 从常见路径找到: ${MSYS2_ROOT_DIR}")
            break()
        endif()
    endforeach()
endif()

# ---- 未找到 MSYS2 根目录 ----
if(NOT MSYS2_ROOT_DIR)
    message(STATUS "[GLFW MSYS2] 无法定位 MSYS2 安装目录，跳过。")
    message(STATUS "             提示: 设置环境变量 MSYS2_ROOT 指向 MSYS2 根目录")
    message(STATUS "             示例: set MSYS2_ROOT=C:\\msys64")
    return()
endif()

# ============================================================================
# 阶段 2：在 MSYS2 目录下搜索 GLFW
# ============================================================================

# MSYS2 中 GLFW 可能安装在 mingw64、ucrt64 或 clang64 子系统下
# 优先搜索编译器自身所在的子系统，再搜索其他子系统
get_filename_component(COMPILER_SUBSYSTEM "${SUBSYSTEM_DIR}" NAME)

set(MINGW_SEARCH_SUBSYSTEMS "${COMPILER_SUBSYSTEM}" "mingw64" "ucrt64" "clang64" "mingw32" "clang32")
list(REMOVE_DUPLICATES MINGW_SEARCH_SUBSYSTEMS)

set(GLFW_FOUND_MSYS2 FALSE)

foreach(_SUB ${MINGW_SEARCH_SUBSYSTEMS})
    set(_INC_DIR "${MSYS2_ROOT_DIR}/${_SUB}/include")
    set(_LIB_DIR "${MSYS2_ROOT_DIR}/${_SUB}/lib")

    # ---- 检查头文件 ----
    if(EXISTS "${_INC_DIR}/GLFW/glfw3.h")
        set(GLFW_INCLUDE_DIRS "${_INC_DIR}")

        # ---- 检查库文件（优先静态库 .a，其次动态导入库 .dll.a）----
        set(GLFW_LIB_FOUND_LOCAL FALSE)

        # 静态库
        set(_STATIC_LIB "${_LIB_DIR}/libglfw3.a")
        if(EXISTS "${_STATIC_LIB}")
            set(GLFW_LIBRARIES "${_STATIC_LIB}")
            set(GLFW_LIBRARY   "${_STATIC_LIB}")
            set(GLFW_LIB_FOUND_LOCAL TRUE)
            message(STATUS "[GLFW MSYS2] 找到静态库: ${_STATIC_LIB}")
        endif()

        # 动态导入库
        if(NOT GLFW_LIB_FOUND_LOCAL)
            set(_DLL_A "${_LIB_DIR}/libglfw3.dll.a")
            if(EXISTS "${_DLL_A}")
                set(GLFW_LIBRARIES "${_DLL_A}")
                set(GLFW_LIBRARY   "${_DLL_A}")
                set(GLFW_LIB_FOUND_LOCAL TRUE)
                message(STATUS "[GLFW MSYS2] 找到动态导入库: ${_DLL_A}")
            endif()
        endif()

        # 也尝试不带 lib 前缀的命名
        if(NOT GLFW_LIB_FOUND_LOCAL)
            set(_ALT_LIB "${_LIB_DIR}/glfw3.dll.a")
            if(EXISTS "${_ALT_LIB}")
                set(GLFW_LIBRARIES "${_ALT_LIB}")
                set(GLFW_LIBRARY   "${_ALT_LIB}")
                set(GLFW_LIB_FOUND_LOCAL TRUE)
                message(STATUS "[GLFW MSYS2] 找到替代命名库: ${_ALT_LIB}")
            endif()
        endif()

        if(GLFW_LIBRARY AND GLFW_INCLUDE_DIRS)
            set(GLFW_FOUND_MSYS2 TRUE)
            set(GLFW_FOUND TRUE)  # 设置全局变量，与 find_package 兼容

            # 创建 IMPORTED 目标使 target_link_libraries(... glfw) 正常工作
            if(NOT TARGET glfw)
                add_library(glfw UNKNOWN IMPORTED)
                set_target_properties(glfw PROPERTIES
                    IMPORTED_LOCATION "${GLFW_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIRS}"
                )
            endif()

            message(STATUS "[GLFW MSYS2] 找到 MSYS2 安装: ${_SUB} [OK]")
            message(STATUS "             头文件: ${GLFW_INCLUDE_DIRS}")
            message(STATUS "             库文件: ${GLFW_LIBRARIES}")
            break()
        endif()
    endif()
endforeach()

# ============================================================================
# 阶段 3：未找到时的诊断信息
# ============================================================================

if(NOT GLFW_FOUND_MSYS2)
    message(WARNING "[GLFW MSYS2] 在 ${MSYS2_ROOT_DIR} 下未找到 GLFW。\n"
                    "  请确认已通过 MSYS2 安装 GLFW：\n"
                    "    根据你的 MinGW 子系统，运行以下命令之一：\n"
                    "    · pacman -S mingw-w64-x86_64-glfw       # mingw64 子系统\n"
                    "    · pacman -S mingw-w64-ucrt-x86_64-glfw  # ucrt64 子系统\n"
                    "    · pacman -S mingw-w64-clang-x86_64-glfw # clang64 子系统\n"
                    "  或设置 MSYS2_ROOT 环境变量指向 MSYS2 根目录：\n"
                    "    set MSYS2_ROOT=C:\\msys64\n"
                    "  或运行诊断脚本:\n"
                    "    scripts\\fix_msys2_glfw.bat")
endif()
