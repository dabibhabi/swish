include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

# RoadScene is split into topic files (MeshBuilder, Surfaces, Furniture, Ramps);
# glob the directory so a new topic file needs no edit here.
file(GLOB ROAD_SCENE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/scene/RoadScene/*.cpp)

add_executable(swish_tests
    test_camera.cpp
    test_road_scene.cpp
    test_pbr.cpp
    test_car_physics.cpp
    test_rain.cpp
    test_corridor.cpp
    test_golden_mesh.cpp
    test_golden_car.cpp
    test_road_params.cpp
    test_rigid_chassis.cpp

    ${CMAKE_SOURCE_DIR}/src/scene/Camera/Camera.cpp
    ${ROAD_SCENE_SOURCES}
    # Phase 3a corridor. CorridorDriver.cpp is deliberately NOT here: it pulls
    # in Renderer (Vulkan) and CarEntity (GLFW input), which the test harness
    # has no business linking. The parts under test — the alignment queries and
    # the drivable-bounds math — live in CorridorAlignment.cpp and in
    # CorridorDriver's own header-free pure methods, so
    # CorridorBounds.cpp carries those without the engine dependency.
    ${CMAKE_SOURCE_DIR}/src/scene/Corridor/CorridorAlignment.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/Corridor/CorridorBounds.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/RoadGeometry/RoadGeometry.cpp
    # Real CarEntity::update() for the golden-trajectory test. No Vulkan
    # dependency; it only needs glfwGetKey (already linked) for handle_input,
    # which the test never calls.
    ${CMAKE_SOURCE_DIR}/src/scene/Entity/CarEntity.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/Entity/Entity.cpp
)

target_include_directories(swish_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Catch2 + GLM for the test harness; glfw is required because Camera.cpp
# (compiled into the test binary) polls input via glfwGetKey().
target_link_libraries(swish_tests PRIVATE
    Catch2::Catch2WithMain
    glm::glm
    glfw
)

target_compile_definitions(swish_tests PRIVATE
    # Match the runtime: Vulkan [0,1] clip-Z (see top-level CMakeLists).
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"
    GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/"
    $<$<STREQUAL:${SWISH_BACKEND},APPLE_METAL>:SWISH_BACKEND_APPLE>
    $<$<STREQUAL:${SWISH_BACKEND},LINUX_VULKAN>:SWISH_BACKEND_LINUX>
)

target_compile_options(swish_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Optional AddressSanitizer + UndefinedBehaviorSanitizer
option(ENABLE_SANITIZERS "Build swish_tests with ASan + UBSan" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(swish_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(swish_tests PRIVATE -fsanitize=address,undefined)
endif()

# road.bin must exist before the test binary runs
add_dependencies(swish_tests bake_configs)

include(Catch)
catch_discover_tests(swish_tests)

# Pure utils tests (src/utils/**), GLFW-free: tests/utils/CMakeLists.txt -> swish_utils_tests.
add_subdirectory(utils)

# DebugParams save/load round-trip. DebugParamsIO.cpp only exists under
# SWISH_DEBUG_UI (needs toml++, no ImGui/Vulkan), so it gets its own small
# executable rather than defining SWISH_DEBUG_UI on swish_tests (which would
# change CarEntity's layout/behaviour under test).
if(SWISH_DEBUG_UI)
    add_executable(swish_debug_tests
        test_debug_params_io.cpp
        utils/test_debug_TomlGlm.cpp
        ${CMAKE_SOURCE_DIR}/src/debug/DebugParamsIO.cpp
    )
    target_include_directories(swish_debug_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(swish_debug_tests PRIVATE Catch2::Catch2WithMain glm::glm tomlplusplus::tomlplusplus)
    target_compile_definitions(swish_debug_tests PRIVATE
        SWISH_DEBUG_UI
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"
    )
    catch_discover_tests(swish_debug_tests)
endif()
