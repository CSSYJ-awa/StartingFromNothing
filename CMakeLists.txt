cmake_minimum_required(VERSION 3.15)
project(VulkanApp LANGUAGES C CXX)

# ============================================================================
# 【环境诊断】在 CMake 配置阶段自动执行，验证所有依赖是否就绪
# 检测项：CMake 版本 / 编译器 / Vulkan SDK / GLFW / GLM
# 若某项 FAIL，将给出修复建议并中止配置（FATAL_ERROR）或警告（WARNING）
# ============================================================================
include(cmake/CheckEnvironment.cmake)

# ============================================================================
# 【MinGW 导入库自动生成】若使用 MinGW 编译器，自动从 vulkan-1.dll
# 生成 libvulkan-1.dll.a，解决 Vulkan SDK 默认 MSVC .lib 不兼容问题。
# 若导入库已存在或编译器非 MinGW，则静默跳过。
# ============================================================================
include(cmake/GenerateVulkanMingwLib.cmake)

# ============================================================================
# C++ 标准
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# 查找依赖
# ============================================================================

# --- Vulkan SDK ---
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan Library: ${Vulkan_LIBRARY}")

# --- GLFW ---
# 三层回退策略：
#   1. 标准 find_package(glfw3)  —— CMake 注册的配置文件
#   2. FindGLFW_MSYS2.cmake       —— 自动探测 MSYS2 安装的 GLFW
#   3. find_library + find_path    —— 手动指定路径（GLFW_DIR 环境变量 / 项目 lib/）
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    message(STATUS "[GLFW] find_package(glfw3) 成功")
    if(DEFINED glfw3_VERSION)
        message(STATUS "[GLFW] 版本: ${glfw3_VERSION}")
    endif()
else()
    # ---- 第 2 层：尝试 MSYS2 自动探测 ----
    message(STATUS "[GLFW] find_package(glfw3) 未找到，尝试 MSYS2 自动探测...")
    include(cmake/FindGLFW_MSYS2.cmake)
endif()

# ---- 第 3 层：通用回退（find_library + find_path）----
if(NOT GLFW_FOUND)
    message(STATUS "[GLFW] MSYS2 探测未找到，使用 find_library 回退...")

    find_library(GLFW_LIBRARY
        NAMES glfw3 glfw3dll glfw
        PATHS
            "$ENV{GLFW_DIR}/lib-mingw-w64"
            "$ENV{GLFW_DIR}/lib"
            "${CMAKE_SOURCE_DIR}/lib"
        DOC "GLFW 静态/导入库路径"
    )

    find_path(GLFW_INCLUDE_DIRS
        NAMES GLFW/glfw3.h
        PATHS
            "$ENV{GLFW_DIR}/include"
            "${CMAKE_SOURCE_DIR}/include"
        DOC "GLFW 头文件路径"
    )

    if(GLFW_LIBRARY AND GLFW_INCLUDE_DIRS)
        message(STATUS "[GLFW] find_library 找到库: ${GLFW_LIBRARY}")
        message(STATUS "[GLFW] find_path   找到头文件: ${GLFW_INCLUDE_DIRS}")
        set(GLFW_LIBRARIES "${GLFW_LIBRARY}")
        set(GLFW_FOUND TRUE)

        if(NOT TARGET glfw)
            add_library(glfw UNKNOWN IMPORTED)
            set_target_properties(glfw PROPERTIES
                IMPORTED_LOCATION "${GLFW_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIRS}"
            )
        endif()
    else()
        message(FATAL_ERROR
            "GLFW 未找到。请通过以下方式之一安装 GLFW：\n"
            "  [推荐 MSYS2] pacman -S mingw-w64-ucrt-x86_64-glfw\n"
            "  [手动下载]  https://www.glfw.org/download.html\n"
            "  [环境变量]  设置 GLFW_DIR 指向 GLFW 安装目录\n"
            "  [诊断脚本]  运行 scripts\\fix_msys2_glfw.bat")
    endif()
endif()

# --- GLM ---
# 两层回退策略：
#   1. 标准 find_package(glm)  —— CMake 注册的配置文件
#   2. FindGLM_MSYS2.cmake      —— 自动探测 MSYS2 安装的 GLM（纯头文件库）
find_package(glm QUIET)
if(glm_FOUND)
    message(STATUS "[GLM] find_package(glm) 成功")
else()
    message(STATUS "[GLM] find_package(glm) 未找到，尝试 MSYS2 自动探测...")
    include(cmake/FindGLM_MSYS2.cmake)
endif()

if(NOT GLM_FOUND)
    # 手动指定 GLM_INCLUDE_DIRS 的最后机会
    find_path(GLM_INCLUDE_DIRS_MANUAL
        NAMES glm/glm.hpp
        PATHS
            "$ENV{GLM_INCLUDE_DIR}"
            "$ENV{GLM_DIR}"
            "$ENV{GLM_ROOT}/include"
            "${CMAKE_SOURCE_DIR}/include"
        DOC "GLM 头文件路径"
    )
    if(GLM_INCLUDE_DIRS_MANUAL)
        set(GLM_INCLUDE_DIRS "${GLM_INCLUDE_DIRS_MANUAL}" CACHE PATH "GLM include directory")
        set(GLM_FOUND TRUE)
        message(STATUS "[GLM] 手动找到头文件: ${GLM_INCLUDE_DIRS}")
    else()
        message(FATAL_ERROR
            "GLM 未找到。GLM 是纯头文件库，可通过以下方式安装：\n"
            "  [推荐 MSYS2] pacman -S mingw-w64-ucrt-x86_64-glm\n"
            "  [手动下载]   https://github.com/g-truc/glm/releases\n"
            "  [环境变量]   设置 GLM_INCLUDE_DIR 指向包含 glm/glm.hpp 的目录")
    endif()
else()
    message(STATUS "[GLM] 包含路径: ${GLM_INCLUDE_DIRS}")
endif()

# ============================================================================
# 着色器编译（自定义命令）
# ============================================================================
# 查找 glslc 编译器（Vulkan SDK 的一部分）
find_program(GLSLC_EXECUTABLE
    NAMES glslc
    PATHS "$ENV{VULKAN_SDK}/Bin"
    DOC "Vulkan GLSL 编译器 (glslc)"
)

# 着色器源文件（使用标准扩展名 .vert / .frag 以便 glslc 识别着色器阶段）
set(SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fragment.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cull.comp"
)

# 为每个着色器创建编译命令
foreach(SHADER ${SHADER_SOURCES})
    # 生成 .spv 路径（在源目录的 shaders/ 下）
    get_filename_component(SHADER_DIR ${SHADER} DIRECTORY)
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SPV_FILE "${SHADER_DIR}/${SHADER_NAME}.spv")

    if(GLSLC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLC_EXECUTABLE} "${SHADER}" -o "${SPV_FILE}"
            DEPENDS "${SHADER}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "编译着色器: ${SHADER} → ${SPV_FILE}"
            VERBATIM
        )
        list(APPEND GENERATED_SPV_FILES ${SPV_FILE})
    else()
        message(WARNING "glslc 未找到，着色器 ${SHADER} 将不会被编译。"
                        "请安装 Vulkan SDK 或手动运行 shaders/compile_shaders.bat")
    endif()
endforeach()

# ============================================================================
# 可执行文件（显式列出所有源文件，而不是 GLOB）
# ============================================================================
set(SOURCES
    src/main.cpp
    src/vulkan_app.cpp
    src/chunk.cpp
    src/octree.cpp
    src/terrain_generator.cpp
    src/player.cpp
    src/world.cpp
    src/optimization_manager.cpp
    src/render_engine.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES} ${GENERATED_SPV_FILES})

# 可执行文件输出到 bin/
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# ---- 若 MinGW 导入库自动生成目标存在，将其设为编译前置依赖 ----
if(TARGET generate_vulkan_mingw_lib)
    add_dependencies(${PROJECT_NAME} generate_vulkan_mingw_lib)
endif()

# ============================================================================
# 包含目录
# ============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
)

# ============================================================================
# 链接库 —— 顺序至关重要（尤其 MinGW）
#
# 【为何 MinGW 下必须手动链接 gdi32、opengl32？】
#   GLFW 的静态库 (libglfw3.a) 在内部调用了 Win32 GDI 函数（如 ChoosePixelFormat）
#   和 OpenGL 函数；MSVC 的 .lib 文件中已记录了这些依赖（通过 #pragma comment(lib)），
#   链接器会自动解析。而 MinGW 的 GNU ld 链接器不会自动传递间接依赖，
#   必须显式列出所有被依赖的系统库，否则会出现 "undefined reference" 错误。
#   同理，链接 m（libm）提供数学函数支持。
# ============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan          # 1. Vulkan Loader
    ${GLFW_LIBRARIES}       # 2. GLFW 窗口库（使用变量兼容 MSYS2 / 手动 / find_package）
    gdi32                   # 3. Windows GDI（GLFW 静态库依赖）
    opengl32                # 4. OpenGL（GLFW 静态库依赖）
    m                       # 5. 数学库
    pthread                 # 6. 线程库（std::thread）
)

# --- 编译定义 ---
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
)

# ============================================================================
# 【注意】不再复制 vulkan-1.dll 到输出目录。
# 运行时使用 C:\Windows\System32\vulkan-1.dll（显卡驱动安装，ABI 稳定），
# 无需复制。详情见 docs/VULKAN_MINGW_SETUP.md。
# ============================================================================

# ============================================================================
# clangd IntelliSense: 禁用响应文件，使 include 路径直接写入
# compile_commands.json，解决 clangd 无法解析 @file.rsp 的问题。
# ============================================================================
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES OFF)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS OFF)

# ============================================================================
# 生成 compile_commands.json（供 IntelliSense / clangd 使用）
# ============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
