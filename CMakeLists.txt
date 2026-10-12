cmake_minimum_required(VERSION 3.20)
project(swish LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Graphics backend selection ---
# AUTO detects the host platform; override with -DSWISH_BACKEND=<value>.
#   APPLE_METAL    -> macOS via MoltenVK (adds Vulkan portability extensions)
#   LINUX_VULKAN   -> Linux with AMD or NVIDIA (native Vulkan, no portability layer)
# The Windows backend was dropped from the supported matrix (2026-09-29).
set(SWISH_BACKEND "AUTO" CACHE STRING
    "Graphics backend: AUTO | APPLE_METAL | LINUX_VULKAN")
set_property(CACHE SWISH_BACKEND PROPERTY STRINGS AUTO APPLE_METAL LINUX_VULKAN)

if(SWISH_BACKEND STREQUAL "AUTO")
    if(APPLE)
        set(SWISH_BACKEND "APPLE_METAL")
    elseif(UNIX)
        set(SWISH_BACKEND "LINUX_VULKAN")
    else()
        message(FATAL_ERROR "swish: unsupported platform (macOS and Linux only) - set SWISH_BACKEND manually")
    endif()
endif()

if(NOT SWISH_BACKEND STREQUAL "APPLE_METAL" AND NOT SWISH_BACKEND STREQUAL "LINUX_VULKAN")
    message(FATAL_ERROR "swish: unsupported SWISH_BACKEND='${SWISH_BACKEND}' (use AUTO | APPLE_METAL | LINUX_VULKAN)")
endif()

message(STATUS "swish: SWISH_BACKEND = ${SWISH_BACKEND}")

# Platform detection (still needed for framework linking on macOS)
if(SWISH_BACKEND STREQUAL "APPLE_METAL")
    message(STATUS "swish: target platform is macOS (MoltenVK / portability)")
else()
    message(STATUS "swish: target platform is Linux (native Vulkan, AMD/NVIDIA)")
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

# Every shader in shaders/ is compiled. CONFIGURE_DEPENDS re-globs at build time,
# so adding/removing a shader triggers a reconfigure automatically.
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
)
list(SORT SHADER_SOURCES)

# Shared GLSL includes (shaders/common/*.glsl, pulled in with #include "x.glsl").
# glslc gets -I on the common dir and emits a depfile (-MD -MF) so Ninja/Makefile
# generators rebuild exactly the shaders that include a touched file. The Xcode
# generator ignores DEPFILE, so every shader also DEPENDS on the globbed includes
# as a coarse fallback (editing any include rebuilds all shaders). Ninja/Makefile
# generators use the exact depfile only.
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_DIR}/common/*.glsl)
list(SORT SHADER_INCLUDES)
set(SHADER_INCLUDE_ARGS -I ${SHADER_DIR}/common -I ${SHADER_DIR})
set(SHADER_USE_DEPFILE FALSE)
if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    set(SHADER_USE_DEPFILE TRUE)
endif()

# In debug builds, shaders that read the scene-params UBO switch on this define
# (release keeps the literal constants → identical output). Empty list = no extra args.
set(SHADER_DEFINES)
if(SWISH_DEBUG_UI)
    set(SHADER_DEFINES -DSWISH_DEBUG_UI)
endif()

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_SPV ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    set(SHADER_DEPFILE_ARGS)
    set(SHADER_DEPFILE_OPT)
    set(SHADER_FALLBACK_DEPS ${SHADER_INCLUDES})
    if(SHADER_USE_DEPFILE)
        set(SHADER_DEPFILE_ARGS -MD -MF ${SHADER_SPV}.d)
        set(SHADER_DEPFILE_OPT DEPFILE ${SHADER_SPV}.d)
        set(SHADER_FALLBACK_DEPS)   # depfile is exact; no need to over-rebuild
    endif()
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} ${SHADER_DEFINES} ${SHADER_INCLUDE_ARGS} ${SHADER_DEPFILE_ARGS}
                ${SHADER} -o ${SHADER_SPV}
        DEPENDS ${SHADER} ${SHADER_FALLBACK_DEPS}
        ${SHADER_DEPFILE_OPT}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPV_FILES})

# --- Shared warning set ---
# One definition of the first-party warning flags, linked by swish, swish_scene_core, the test targets and
# toml_baker. Third-party headers are SYSTEM includes, so -Werror only ever fires on first-party code.
# SWISH_WERROR is opt-in (CI turns it on).
option(SWISH_WERROR "Treat warnings as errors in first-party swish sources" OFF)
add_library(swish_warnings INTERFACE)
target_compile_options(swish_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
)
if(SWISH_WERROR)
    target_compile_options(swish_warnings INTERFACE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>)
endif()

# --- swish_utils: header-only utils (src/utils/**) as a target: include root + GLM + clip-Z convention ---
# Vulkan clip space uses [0,1] depth, not OpenGL's [-1,1]. This makes glm::perspective emit [0,1] clip-Z so the
# depth buffer and the shader depth->world reconstruction (lighting.frag / ssao.frag) agree.
add_library(swish_utils INTERFACE)
target_include_directories(swish_utils INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(swish_utils INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_link_libraries(swish_utils INTERFACE glm::glm)

# --- swish_scene_core: the GPU-free scene sources the test binaries link ---
# RoadScene is split into topic files (MeshBuilder, Surfaces, Furniture, Ramps); glob the directory so a new
# topic file needs no edit here. CorridorDriver.cpp is deliberately NOT here: it pulls in Renderer (Vulkan) and
# CarEntity (GLFW input), which the test harness has no business linking. The parts under test (alignment
# queries, drivable-bounds math) live in CorridorAlignment.cpp / CorridorBounds.cpp. CarEntity.cpp is the real
# CarEntity::update() for the golden-trajectory test; it needs only glfwGetKey (handle_input, never called by
# the tests). The main `swish` executable still compiles every src/ file itself (it is not linked against this).
file(GLOB SWISH_ROAD_SCENE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/scene/RoadScene/*.cpp)
list(SORT SWISH_ROAD_SCENE_SOURCES)
add_library(swish_scene_core STATIC
    ${CMAKE_SOURCE_DIR}/src/scene/Camera/Camera.cpp
    ${SWISH_ROAD_SCENE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/scene/Corridor/CorridorAlignment.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/Corridor/CorridorBounds.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/RoadGeometry/RoadGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/Entity/CarEntity.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/Entity/Entity.cpp
)
target_include_directories(swish_scene_core SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/include)   # vendored stb_image.h
target_link_libraries(swish_scene_core
    PUBLIC  swish_utils glfw          # Camera.cpp polls input via glfwGetKey()
    PRIVATE swish_warnings
)
target_compile_definitions(swish_scene_core PRIVATE
    CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"   # RoadScene() default ctor loads road.bin
)
target_compile_definitions(swish_scene_core PUBLIC
    $<$<STREQUAL:${SWISH_BACKEND},APPLE_METAL>:SWISH_BACKEND_APPLE>
    $<$<STREQUAL:${SWISH_BACKEND},LINUX_VULKAN>:SWISH_BACKEND_LINUX>
)

# --- Config baker (TOML → binary) ---
add_executable(toml_baker tools/toml_baker.cpp)
target_link_libraries(toml_baker PRIVATE tomlplusplus::tomlplusplus swish_warnings)
target_compile_features(toml_baker PRIVATE cxx_std_23)
# Include root: same "scene/...", "utils/..." paths as the game (it shares RoadConfig.h and Units.h).
target_include_directories(toml_baker PRIVATE ${CMAKE_SOURCE_DIR}/src)

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

# Runtime TOML configs (parsed with toml++ at startup in both builds, not baked): copied next to road.bin so the
# game and the tests read CONFIG_DIR "car.toml". configure_file re-runs CMake when the source changes.
configure_file(${CMAKE_SOURCE_DIR}/config/car.toml ${CONFIG_DIR}/car.toml COPYONLY)   # CarConfig.cpp

# --- Main executable ---
# Sources are globbed (CONFIGURE_DEPENDS): new .cpp files under src/ are picked up
# automatically, no list to edit. src/debug/ (Dear ImGui live-tuning UI) is only
# compiled when SWISH_DEBUG_UI=ON; it is filtered out of the release glob below.
file(GLOB_RECURSE SWISH_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER SWISH_SOURCES EXCLUDE REGEX "/src/debug/")
list(SORT SWISH_SOURCES)
add_executable(swish ${SWISH_SOURCES})

add_dependencies(swish shaders bake_configs)

# Include root: project headers are included as "module/Name/Name.h" rooted at src/
# (e.g. "utils/units/Units.h", "renderer/Renderer/Renderer.h"); only same-directory
# includes are bare ("Renderer.h"). No "../" in any #include.
target_include_directories(swish PRIVATE
    ${CMAKE_SOURCE_DIR}/shaders/common            # bindings.glsl, #included by renderer/Bindings.h
)

target_compile_definitions(swish PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}/"
    TEXTURE_DIR="${CMAKE_SOURCE_DIR}/textures/"
    ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/"
    CONFIG_DIR="${CONFIG_DIR}/"
    $<$<STREQUAL:${SWISH_BACKEND},APPLE_METAL>:SWISH_BACKEND_APPLE>
    $<$<STREQUAL:${SWISH_BACKEND},LINUX_VULKAN>:SWISH_BACKEND_LINUX>
)

# Third-party headers are marked SYSTEM so -Wall/-Wextra/-Wpedantic and -Werror only ever fire on
# first-party code: tinygltf's bundled json.hpp, VMA's vk_mem_alloc.h (Clang nullability extensions trip
# -Wpedantic on AppleClang) and the vendored include/ (stb_image.h).
target_include_directories(swish SYSTEM PRIVATE
    ${tinygltf_SOURCE_DIR}
    ${vulkanmemoryallocator_SOURCE_DIR}/include   # vk_mem_alloc.h
    ${CMAKE_SOURCE_DIR}/include                   # vendored stb_image.h
)

target_link_libraries(swish PRIVATE
    swish_utils
    swish_warnings
    glfw
    Vulkan::Vulkan
    tinygltf
    tomlplusplus::tomlplusplus   # CarConfig.cpp reads config/car.toml in both builds
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
    file(GLOB SWISH_DEBUG_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/debug/*.cpp)
    list(SORT SWISH_DEBUG_SOURCES)
    target_sources(swish PRIVATE ${SWISH_DEBUG_SOURCES})
    target_link_libraries(swish PRIVATE imgui tomlplusplus::tomlplusplus)
    target_compile_definitions(swish PRIVATE SWISH_DEBUG_UI
        # Shader hot-reload (renderer/ShaderHotReload): where the GLSL sources live and the glslc CMake found
        # (the runtime looks at $VULKAN_SDK/bin first, like find_program above).
        SWISH_SHADER_SRC_DIR="${SHADER_DIR}"
        SWISH_GLSLC_PATH="${GLSLC}"
    )
    message(STATUS "swish: Dear ImGui debug UI ENABLED (make debug -> build-debug/)")
endif()

# --- Tests ---
enable_testing()
include(CTest)
add_subdirectory(tests)
