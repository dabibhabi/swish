cmake_minimum_required(VERSION 3.20)
project(swish LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Graphics backend selection ---
# AUTO detects the host platform; override with -DSWISH_BACKEND=<value>.
#   APPLE_METAL    → macOS via MoltenVK (adds Vulkan portability extensions)
#   LINUX_VULKAN   → Linux with AMD or NVIDIA (native Vulkan, no portability layer)
#   WINDOWS_VULKAN → Windows with AMD or NVIDIA (native Vulkan)
set(SWISH_BACKEND "AUTO" CACHE STRING
    "Graphics backend: AUTO | APPLE_METAL | LINUX_VULKAN | WINDOWS_VULKAN")
set_property(CACHE SWISH_BACKEND PROPERTY STRINGS AUTO APPLE_METAL LINUX_VULKAN WINDOWS_VULKAN)

if(SWISH_BACKEND STREQUAL "AUTO")
    if(APPLE)
        set(SWISH_BACKEND "APPLE_METAL")
    elseif(UNIX)
        set(SWISH_BACKEND "LINUX_VULKAN")
    elseif(WIN32)
        set(SWISH_BACKEND "WINDOWS_VULKAN")
    else()
        message(FATAL_ERROR "swish: unsupported platform — set SWISH_BACKEND manually")
    endif()
endif()

message(STATUS "swish: SWISH_BACKEND = ${SWISH_BACKEND}")

# Platform detection (still needed for framework linking on macOS)
if(SWISH_BACKEND STREQUAL "APPLE_METAL")
    message(STATUS "swish: target platform is macOS (MoltenVK / portability)")
elseif(SWISH_BACKEND STREQUAL "LINUX_VULKAN")
    message(STATUS "swish: target platform is Linux (native Vulkan, AMD/NVIDIA)")
elseif(SWISH_BACKEND STREQUAL "WINDOWS_VULKAN")
    message(STATUS "swish: target platform is Windows (native Vulkan, AMD/NVIDIA)")
endif()

# --- GLFW (Homebrew on macOS, system package on Linux) ---
find_package(glfw3 3.3 REQUIRED)

# --- GLM (header-only) ---
find_package(glm REQUIRED)

# --- macOS frameworks (needed for GLFW internals on Apple) ---
if(SWISH_BACKEND STREQUAL "APPLE_METAL")
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
    ${SHADER_DIR}/rain.vert
    ${SHADER_DIR}/rain.frag
    ${SHADER_DIR}/glass.vert
    ${SHADER_DIR}/glass.frag
    ${SHADER_DIR}/windshield_rain.vert
    ${SHADER_DIR}/windshield_rain.frag
    ${SHADER_DIR}/windshield_wetness.frag
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
    src/renderer/RainSystem/RainSystem.cpp
    src/renderer/GlassPass/GlassPass.cpp
    src/renderer/WindshieldRainPass/WindshieldRainPass.cpp
    src/utils/StbImage.cpp
)

add_dependencies(swish shaders bake_configs)

target_include_directories(swish PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_definitions(swish PRIVATE
    # Vulkan clip space uses [0,1] depth, not OpenGL's [-1,1]. This makes
    # glm::perspective emit [0,1] clip-Z so the depth buffer and the shader
    # depth->world reconstruction (lighting.frag / ssao.frag) agree.
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    SHADER_DIR="${SHADER_OUTPUT_DIR}/"
    TEXTURE_DIR="${CMAKE_SOURCE_DIR}/textures/"
    ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/"
    CONFIG_DIR="${CONFIG_DIR}/"
    $<$<STREQUAL:${SWISH_BACKEND},APPLE_METAL>:SWISH_BACKEND_APPLE>
    $<$<STREQUAL:${SWISH_BACKEND},LINUX_VULKAN>:SWISH_BACKEND_LINUX>
    $<$<STREQUAL:${SWISH_BACKEND},WINDOWS_VULKAN>:SWISH_BACKEND_WINDOWS>
)

target_compile_options(swish PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_link_libraries(swish PRIVATE
    glad
    glfw
    glm::glm
    Vulkan::Vulkan
    tinygltf
)

if(SWISH_BACKEND STREQUAL "APPLE_METAL")
    target_link_libraries(swish PRIVATE
        ${OPENGL_LIBRARY}
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
    )
endif()

# --- Tests ---
enable_testing()
include(CTest)
add_subdirectory(tests)
