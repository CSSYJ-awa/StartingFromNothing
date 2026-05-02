cmake_minimum_required(VERSION 3.16)
project(vulkan_mingw_example LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# User-provided Vulkan SDK path (can be overridden on command line)
set(VULKAN_SDK "D:/Program Files/VulkanSDK/1.4.341.1" CACHE PATH "Vulkan SDK path")
if (WIN32)
  # expose env var for some build systems/tools
  set(ENV{VULKAN_SDK} ${VULKAN_SDK})
endif()

# Help CMake find the Vulkan package shipped with the SDK
# Prefer explicit VulkanConfig location from the SDK
set(Vulkan_DIR "${VULKAN_SDK}/Lib/cmake/vulkan" CACHE PATH "Vulkan CMake config dir")
list(APPEND CMAKE_PREFIX_PATH "${Vulkan_DIR}" "${VULKAN_SDK}/Lib/cmake")
find_package(Vulkan REQUIRED)

# Use bundled glfw (assumed to be in glfw-3.4 folder at repo root)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# glm is header-only; create an INTERFACE target pointing to the folder 'glm'
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/glm)

# Generate SPIR-V from GLSL shaders at configure time
set(SHADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SPVS_DIR ${CMAKE_BINARY_DIR}/shaders)
set(GEN_INCLUDE_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SPVS_DIR} ${GEN_INCLUDE_DIR})

# Compile shaders using glslangValidator from Vulkan SDK
add_custom_command(
  OUTPUT ${SPVS_DIR}/triangle.vert.spv
  COMMAND "${VULKAN_SDK}/Bin/glslangValidator.exe" -V -o "${SPVS_DIR}/triangle.vert.spv" "${SHADERS_DIR}/triangle.vert"
  DEPENDS "${SHADERS_DIR}/triangle.vert"
  VERBATIM
)
add_custom_command(
  OUTPUT ${SPVS_DIR}/triangle.frag.spv
  COMMAND "${VULKAN_SDK}/Bin/glslangValidator.exe" -V -o "${SPVS_DIR}/triangle.frag.spv" "${SHADERS_DIR}/triangle.frag"
  DEPENDS "${SHADERS_DIR}/triangle.frag"
  VERBATIM
)

# Embed SPV into headers
add_custom_command(
  OUTPUT ${GEN_INCLUDE_DIR}/vert_spv.h
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.bat" "${SPVS_DIR}/triangle.vert.spv" "${GEN_INCLUDE_DIR}/vert_spv.h" vert_spv
  DEPENDS ${SPVS_DIR}/triangle.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.ps1
  VERBATIM
)
add_custom_command(
  OUTPUT ${GEN_INCLUDE_DIR}/frag_spv.h
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.bat" "${SPVS_DIR}/triangle.frag.spv" "${GEN_INCLUDE_DIR}/frag_spv.h" frag_spv
  DEPENDS ${SPVS_DIR}/triangle.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.ps1
  VERBATIM
)

add_custom_target(shaders_all DEPENDS ${GEN_INCLUDE_DIR}/vert_spv.h ${GEN_INCLUDE_DIR}/frag_spv.h)

# Example executable named main (produce main.exe)
add_executable(main src/main_triangle.cpp ${GEN_INCLUDE_DIR}/vert_spv.h ${GEN_INCLUDE_DIR}/frag_spv.h)
add_dependencies(main shaders_all)
# Ensure Vulkan headers are found
target_include_directories(main PRIVATE "${VULKAN_SDK}/Include" "${GEN_INCLUDE_DIR}")
# Link Vulkan and GLFW
target_link_libraries(main PRIVATE Vulkan::Vulkan glfw glm)
# Enable validation layers for debugging
target_compile_definitions(main PRIVATE ENABLE_VALIDATION=1)
set_target_properties(main PROPERTIES OUTPUT_NAME "main")

# Voxel prototype target
add_executable(voxel src/voxel_game.cpp ${GEN_INCLUDE_DIR}/vert_spv.h ${GEN_INCLUDE_DIR}/frag_spv.h)
add_dependencies(voxel shaders_all)
target_include_directories(voxel PRIVATE "${VULKAN_SDK}/Include" "${GEN_INCLUDE_DIR}")
target_link_libraries(voxel PRIVATE Vulkan::Vulkan glfw glm)
target_compile_definitions(voxel PRIVATE ENABLE_VALIDATION=1)
set_target_properties(voxel PROPERTIES OUTPUT_NAME "voxel")

# Install targets for convenience (optional)
install(TARGETS main voxel RUNTIME DESTINATION bin)
