cmake_minimum_required(VERSION 3.20)
project(swish LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Platform detection ---
# APPLE → needs Cocoa/IOKit/OpenGL frameworks for GLFW's internals.
# Linux and Windows → GLFW's own package config pulls in the display-server
# dependencies transitively, so no per-platform libs are needed here.
if(APPLE)
    message(STATUS "swish: target platform is macOS")
elseif(UNIX)
    message(STATUS "swish: target platform is Linux")
elseif(WIN32)
    message(STATUS "swish: target platform is Windows")
else()
    message(FATAL_ERROR "swish: unsupported platform")
endif()

# --- GLFW (Homebrew on macOS, system package on Linux) ---
find_package(glfw3 3.3 REQUIRED)

# --- GLM (header-only) ---
find_package(glm REQUIRED)

# --- macOS frameworks ---
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(IOKIT_LIBRARY IOKit REQUIRED)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
endif()

# --- Vulkan ---
find_package(Vulkan REQUIRED)

# --- toml++ (header-only, for config baker) ---
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# --- tinygltf (header-only GLB/glTF loader) ---
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.3
)
FetchContent_MakeAvailable(tinygltf)

# --- GLAD (compiled as part of the project) ---
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/include)

# --- Shader compilation ---
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Make sure Vulkan SDK is installed.")
endif()

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SHADER_SOURCES
    ${SHADER_DIR}/basic.vert
    ${SHADER_DIR}/basic.frag
    ${SHADER_DIR}/fullscreen.vert
    ${SHADER_DIR}/bloom_extract.frag
    ${SHADER_DIR}/bloom_blur.frag
    ${SHADER_DIR}/composite.frag
    ${SHADER_DIR}/gbuffer.frag
    ${SHADER_DIR}/lighting.frag
)

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_SPV ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} ${SHADER} -o ${SHADER_SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPV_FILES})

# --- Config baker (TOML → binary) ---
add_executable(toml_baker tools/toml_baker.cpp)
target_link_libraries(toml_baker PRIVATE tomlplusplus::tomlplusplus)
target_compile_features(toml_baker PRIVATE cxx_std_17)

set(CONFIG_DIR ${CMAKE_BINARY_DIR}/config)
file(MAKE_DIRECTORY ${CONFIG_DIR})

set(ROAD_TOML ${CMAKE_SOURCE_DIR}/config/road.toml)
set(ROAD_BIN  ${CONFIG_DIR}/road.bin)

add_custom_command(
    OUTPUT ${ROAD_BIN}
    COMMAND toml_baker ${ROAD_TOML} ${ROAD_BIN}
    DEPENDS toml_baker ${ROAD_TOML}
    COMMENT "Baking road.toml -> road.bin"
)

add_custom_target(bake_configs DEPENDS ${ROAD_BIN})

# --- Main executable ---
add_executable(swish
    src/main.cpp
    src/core/Window/Window.cpp
    src/core/App/App.cpp
    src/renderer/VulkanContext/VulkanContext.cpp
    src/renderer/Renderer/Renderer.cpp
    src/renderer/Pipeline/Device/Device.cpp
    src/renderer/Swapchain/Swapchain.cpp
    src/renderer/RenderPass/RenderPass.cpp
    src/renderer/CommandManager/CommandManager.cpp
    src/renderer/SyncObjects/SyncObjects.cpp
    src/renderer/ResourceManager/ResourceManager.cpp
    src/renderer/Pipeline/Pipeline.cpp
    src/utils/FileIO/FileIO.cpp
    src/scene/Camera/Camera.cpp
    src/scene/RoadScene/RoadScene.cpp
    src/scene/SceneManager/SceneManager.cpp
    src/scene/ModelManager/ModelManager.cpp
    src/scene/Entity/Entity.cpp
    src/scene/Entity/CarEntity.cpp
    src/renderer/TextureManager/TextureManager.cpp
    src/renderer/PostProcessManager/PostProcessManager.cpp
    src/renderer/CameraUniforms/CameraUniforms.cpp
    src/renderer/MaterialDescriptors/MaterialDescriptors.cpp
    src/renderer/DepthBuffer/DepthBuffer.cpp
    src/renderer/SceneGeometry/SceneGeometry.cpp
    src/renderer/ScenePipeline/ScenePipeline.cpp
    src/renderer/DeferredLightingPipeline/DeferredLightingPipeline.cpp
    src/utils/StbImage.cpp
)

add_dependencies(swish shaders bake_configs)

target_include_directories(swish PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_definitions(swish PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}/"
    TEXTURE_DIR="${CMAKE_SOURCE_DIR}/textures/"
    ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/"
    CONFIG_DIR="${CONFIG_DIR}/"
)

target_link_libraries(swish PRIVATE
    glad
    glfw
    glm::glm
    Vulkan::Vulkan
    tinygltf
)

if(APPLE)
    target_link_libraries(swish PRIVATE
        ${OPENGL_LIBRARY}
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
    )
endif()

# --- Tests ---
enable_testing()
add_subdirectory(tests)
