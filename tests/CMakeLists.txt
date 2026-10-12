include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(swish_tests
    test_camera.cpp
    test_road_scene.cpp
    test_pbr.cpp
    test_car_physics.cpp
    test_slip_visuals.cpp
    test_rain.cpp
    test_wheel_spray.cpp
    test_lens_wetness.cpp
    test_corridor.cpp
    test_golden_mesh.cpp
    test_golden_car.cpp
    test_road_params.cpp
    test_car_lights.cpp
    test_car_rig.cpp
    test_rigid_chassis.cpp
    test_input.cpp
    test_bench.cpp
    test_telemetry.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/telemetry/TelemetryRing.cpp
    test_powertrain_invariants.cpp
)

# The scene sources under test (Camera, RoadScene, Corridor alignment/bounds, RoadGeometry, CarEntity) live in
# swish_scene_core (top-level CMakeLists.txt); it brings src/ includes, GLM, glfw and the backend defines.
target_link_libraries(swish_tests PRIVATE
    swish_scene_core
    Catch2::Catch2WithMain
    swish_warnings
)

target_compile_definitions(swish_tests PRIVATE
    CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"
    GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/"
    ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/"
)

# test_car_rig.cpp parses assets/blend/porsche_rigged.glb with tinygltf (header-only; the test TU holds the
# implementation, since swish_tests does not link ModelManager.cpp). SYSTEM: its warnings are not ours.
target_include_directories(swish_tests SYSTEM PRIVATE ${tinygltf_SOURCE_DIR})

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
    target_include_directories(swish_debug_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(swish_debug_tests PRIVATE Catch2::Catch2WithMain swish_utils swish_warnings tomlplusplus::tomlplusplus)
    target_compile_definitions(swish_debug_tests PRIVATE
        SWISH_DEBUG_UI
        CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"
    )
    catch_discover_tests(swish_debug_tests)
endif()
