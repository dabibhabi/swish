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

# --- Debug UI (Dear ImGui) — opt-in via `make debug` / -DSWISH_DEBUG_UI=ON ---
# When OFF (the default / `make run`), none of the ImGui code or the scene-params
# UBO shader path is compiled, so release output is unchanged.
option(SWISH_DEBUG_UI "Build the in-engine Dear ImGui debug/tuning UI" OFF)

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

# --- VulkanMemoryAllocator (header-only; single sub-allocator, RAII handles) ---
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# --- Dear ImGui (debug UI only) — no upstream CMakeLists, so build a small static lib ---
if(SWISH_DEBUG_UI)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
    )
    FetchContent_MakeAvailable(imgui)

    # ImGuizmo (transform gizmos) — populated without its own CMakeLists (which
    # would try to build against an `imgui` target of its own); we compile its one
    # .cpp into our imgui lib so it shares the same ImGui headers.
    FetchContent_Declare(
        imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG        master
    )
    FetchContent_MakeAvailable(imguizmo)
    # ImGuizmo ships a CMakeLists that builds a standalone `imguizmo` target with no
    # ImGui includes (it fetches imgui only for its example). We don't use it — we
    # compile src/ImGuizmo.cpp into our own imgui lib below — so drop it from `all`.
    if(TARGET imguizmo)
        set_target_properties(imguizmo PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
    )
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imguizmo_SOURCE_DIR}/src
    )
    target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
endif()

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
    ${SHADER_DIR}/ssao.frag
    ${SHADER_DIR}/ao_blur.frag
    ${SHADER_DIR}/ssr.frag
    ${SHADER_DIR}/composite.frag
    ${SHADER_DIR}/gbuffer.frag
    ${SHADER_DIR}/lighting.frag
    ${SHADER_DIR}/depth_only.vert
    ${SHADER_DIR}/depth_only.frag
    ${SHADER_DIR}/rain.vert
    ${SHADER_DIR}/rain.frag
    ${SHADER_DIR}/glass.vert
    ${SHADER_DIR}/glass.frag
    ${SHADER_DIR}/windshield_rain.vert
    ${SHADER_DIR}/windshield_rain.frag
    ${SHADER_DIR}/windshield_wetness.frag
)

# In debug builds, shaders that read the scene-params UBO switch on this define
# (release keeps the literal constants → identical output). Empty list = no extra args.
set(SHADER_DEFINES)
if(SWISH_DEBUG_UI)
    set(SHADER_DEFINES -DSWISH_DEBUG_UI)
endif()

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_SPV ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} ${SHADER_DEFINES} ${SHADER} -o ${SHADER_SPV}
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
    src/renderer/CommandManager/CommandManager.cpp
    src/renderer/SyncObjects/SyncObjects.cpp
    src/renderer/ResourceManager/ResourceManager.cpp
    src/renderer/VmaUsage.cpp
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
    src/renderer/DepthOnlyPipeline/DepthOnlyPipeline.cpp
    src/renderer/RainSystem/RainSystem.cpp
    src/renderer/GlassPass/GlassPass.cpp
    src/renderer/WindshieldRainPass/WindshieldRainPass.cpp
    src/utils/StbImage.cpp
)

add_dependencies(swish shaders bake_configs)

target_include_directories(swish PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${vulkanmemoryallocator_SOURCE_DIR}/include   # vk_mem_alloc.h
)

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

# --- Debug UI wiring (compiled only when SWISH_DEBUG_UI) ---
if(SWISH_DEBUG_UI)
    target_sources(swish PRIVATE src/debug/DebugUI.cpp src/debug/SceneParamsUniform.cpp src/debug/DebugParamsIO.cpp)
    target_link_libraries(swish PRIVATE imgui tomlplusplus::tomlplusplus)
    target_compile_definitions(swish PRIVATE SWISH_DEBUG_UI)
    message(STATUS "swish: Dear ImGui debug UI ENABLED (make debug)")
endif()

# --- Tests ---
enable_testing()
include(CTest)
add_subdirectory(tests)
