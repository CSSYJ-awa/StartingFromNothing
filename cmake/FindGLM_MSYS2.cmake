# ==============================================================================
# FindGLM_MSYS2.cmake
#
# 用途：当标准 find_package(glm) 失败时，自动在 MSYS2 安装路径下搜索 GLM。
#
# 背景：
#   GLM 是纯头文件库，MSYS2 将其安装在 /mingw64/include/glm/ 下。
#   本模块自动推断 MSYS2 根目录，设置包含路径供后续构建使用。
#
# 调用方式：在 find_package(glm QUIET) 失败后 include：
#   if(NOT glm_FOUND)
#       include(cmake/FindGLM_MSYS2.cmake)
#   endif()
#
# 输出变量：
#   GLM_FOUND          - TRUE 或 FALSE
#   GLM_INCLUDE_DIRS   - 头文件路径
# ==============================================================================

# ---- 仅在 MinGW 环境下激活 ----
if(NOT MINGW)
    message(STATUS "[GLM MSYS2] 非 MinGW 编译器，跳过 MSYS2 检测。")
    return()
endif()

# ============================================================================
# 阶段 1：推断 MSYS2 根目录（复用 FindGLFW_MSYS2 的逻辑）
# ============================================================================

set(GLM_MSYS2_ROOT "")

# ---- 1.1 从环境变量 MSYS2_ROOT 读取 ----
if(DEFINED ENV{MSYS2_ROOT})
    file(TO_CMAKE_PATH "$ENV{MSYS2_ROOT}" GLM_MSYS2_ROOT)
endif()

# ---- 1.2 从编译器路径反推 ----
if(NOT GLM_MSYS2_ROOT)
    get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(SUBSYSTEM_DIR "${COMPILER_DIR}" DIRECTORY)
    get_filename_component(MAYBE_ROOT  "${SUBSYSTEM_DIR}"  DIRECTORY)

    set(MSYS2_SIG_DIRS "mingw64;ucrt64;clang64;mingw32;clang32")
    foreach(_SUB ${MSYS2_SIG_DIRS})
        if(IS_DIRECTORY "${MAYBE_ROOT}/${_SUB}")
            set(GLM_MSYS2_ROOT "${MAYBE_ROOT}")
            break()
        endif()
    endforeach()
endif()

# ---- 1.3 尝试常见路径 ----
if(NOT GLM_MSYS2_ROOT)
    foreach(_CANDIDATE C:/msys64 C:/msys2 D:/msys64 D:/msys2 E:/msys64 E:/msys2)
        if(IS_DIRECTORY "${_CANDIDATE}")
            set(GLM_MSYS2_ROOT "${_CANDIDATE}")
            break()
        endif()
    endforeach()
endif()

if(NOT GLM_MSYS2_ROOT)
    message(STATUS "[GLM MSYS2] 无法定位 MSYS2 安装目录，跳过。")
    return()
endif()

# ============================================================================
# 阶段 2：在 MSYS2 目录下搜索 GLM 头文件
# ============================================================================

get_filename_component(COMPILER_SUBSYSTEM "${SUBSYSTEM_DIR}" NAME)
set(MINGW_SEARCH_SUBSYSTEMS "${COMPILER_SUBSYSTEM}" "mingw64" "ucrt64" "clang64" "mingw32" "clang32")
list(REMOVE_DUPLICATES MINGW_SEARCH_SUBSYSTEMS)

set(GLM_FOUND_MSYS2 FALSE)

foreach(_SUB ${MINGW_SEARCH_SUBSYSTEMS})
    set(_INC_DIR "${GLM_MSYS2_ROOT}/${_SUB}/include")

    # GLM 是纯头文件库，只需检查 glm/glm.hpp
    if(EXISTS "${_INC_DIR}/glm/glm.hpp")
        set(GLM_INCLUDE_DIRS "${_INC_DIR}" CACHE PATH "GLM include directory (MSYS2)")
        set(GLM_FOUND_MSYS2 TRUE)
        set(GLM_FOUND TRUE)
        set(glm_FOUND TRUE)  # 兼容 find_package(glm) 的变量名约定

        message(STATUS "[GLM MSYS2] 找到 MSYS2 安装: ${_SUB} [OK]")
        message(STATUS "             头文件: ${_INC_DIR}/glm/glm.hpp")
        message(STATUS "             GLM 是纯头文件库，无需链接。")
        break()
    endif()
endforeach()

# ============================================================================
# 阶段 3：未找到时的诊断信息
# ============================================================================

if(NOT GLM_FOUND_MSYS2)
    message(WARNING "[GLM MSYS2] 在 ${GLM_MSYS2_ROOT} 下未找到 GLM。\n"
                    "  GLM 是纯头文件库，可通过以下方式安装：\n"
                    "    · pacman -S mingw-w64-x86_64-glm       # mingw64 子系统\n"
                    "    · pacman -S mingw-w64-ucrt-x86_64-glm  # ucrt64 子系统\n"
                    "    · pacman -S mingw-w64-clang-x86_64-glm # clang64 子系统\n"
                    "  或手动下载 GLM 并解压到任意位置：\n"
                    "    https://github.com/g-truc/glm/releases\n"
                    "  设置环境变量 GLM_INCLUDE_DIR 指向包含 glm/glm.hpp 的目录。")
endif()
