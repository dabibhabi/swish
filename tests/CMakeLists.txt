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
    test_file_io.cpp

    ${CMAKE_SOURCE_DIR}/src/scene/Camera/Camera.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/RoadScene/RoadScene.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/FileIO/FileIO.cpp
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
    CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"
    $<$<STREQUAL:${SWISH_BACKEND},APPLE_METAL>:SWISH_BACKEND_APPLE>
    $<$<STREQUAL:${SWISH_BACKEND},LINUX_VULKAN>:SWISH_BACKEND_LINUX>
    $<$<STREQUAL:${SWISH_BACKEND},WINDOWS_VULKAN>:SWISH_BACKEND_WINDOWS>
)

target_compile_options(swish_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
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
