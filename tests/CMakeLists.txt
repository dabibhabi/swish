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

target_link_libraries(swish_tests PRIVATE
    Catch2::Catch2WithMain
    glm::glm
    Vulkan::Vulkan
    glfw
)

if(APPLE)
    target_link_libraries(swish_tests PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${OPENGL_LIBRARY}
    )
endif()

# road.bin must exist before the test binary runs
add_dependencies(swish_tests bake_configs)

target_compile_definitions(swish_tests PRIVATE
    CONFIG_DIR="${CMAKE_BINARY_DIR}/config/"
)

include(CTest)
include(Catch)
catch_discover_tests(swish_tests)
