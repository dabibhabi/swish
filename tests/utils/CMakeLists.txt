# Utils test target: one test_<topic>_<Header>.cpp per src/utils header (see src/utils/README.md).
# GLFW-free by construction: it links only Catch2 + GLM (+ the Vulkan *headers* for utils/vulkan/,
# never the loader). The .cpp files under test are utils/io/File.cpp and the two Vulkan-free bookkeeping classes of
# renderer/Passes (PassRegistry, ImageRegistry).
# The Camera / RoadScene / CarEntity sources stay in swish_tests (they need GLFW).
add_executable(swish_utils_tests
    test_units_Units.cpp
    test_types_Types.cpp
    test_math_Smoothing.cpp
    test_math_Noise.cpp
    test_math_Hash.cpp
    test_math_Sequences.cpp
    test_physics_RigidBody.cpp
    test_geometry_Frustum.cpp
    test_geometry_Polyline.cpp
    test_color_Color.cpp
    test_container_VectorOps.cpp
    test_meta_EnumCycle.cpp
    test_meta_Tags.cpp
    test_io_File.cpp
    test_vulkan_Check.cpp
    test_vulkan_Init.cpp
    test_vulkan_PushConstants.cpp
    test_vulkan_Barrier.cpp
    test_vulkan_Rendering.cpp
    test_vulkan_RenderPass.cpp
    test_vulkan_Descriptors.cpp
    test_vulkan_Samplers.cpp
    test_vulkan_ImageView.cpp
    test_renderer_Bindings.cpp
    test_renderer_PassRegistry.cpp
    test_renderer_PassDeclaration.cpp
    test_renderer_ShaderHotReload.cpp
    test_renderer_TimestampMath.cpp
    test_renderer_CapturePath.cpp

    ${CMAKE_SOURCE_DIR}/src/utils/io/File.cpp
    # renderer/Passes: the registry + image registry are Vulkan-handle bookkeeping only (no Vulkan calls)
    ${CMAKE_SOURCE_DIR}/src/renderer/Passes/PassRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/renderer/Passes/ImageRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/renderer/Passes/PassDeclarationValidator.cpp
    # Shader hot-reload (S5): pure parts + injectable edges; PipelineRebuild is a std::function registry.
    ${CMAKE_SOURCE_DIR}/src/renderer/PipelineRebuild/PipelineRebuild.cpp
    ${CMAKE_SOURCE_DIR}/src/renderer/ShaderHotReload/ShaderHotReload.cpp
)

# ShaderHotReload.cpp is an empty TU unless SWISH_DEBUG_UI is defined (release binary unchanged). Define it for that
# one source only: its header has no #ifdef, so this does not change any type seen by the other test sources.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/renderer/ShaderHotReload/ShaderHotReload.cpp
    PROPERTIES COMPILE_DEFINITIONS SWISH_DEBUG_UI)

target_include_directories(swish_utils_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shaders/common   # renderer/Bindings.h includes bindings.glsl
)

target_link_libraries(swish_utils_tests PRIVATE Catch2::Catch2WithMain glm::glm)

# utils/vulkan/*.h include <vulkan/vulkan.h> but the tests call no Vulkan function: headers only.
if(TARGET Vulkan::Headers)
    target_link_libraries(swish_utils_tests PRIVATE Vulkan::Headers)
else()
    target_include_directories(swish_utils_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

target_compile_definitions(swish_utils_tests PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE   # match the runtime clip-Z convention (Frustum test)
)

target_compile_options(swish_utils_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
)

if(ENABLE_SANITIZERS)
    target_compile_options(swish_utils_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(swish_utils_tests PRIVATE -fsanitize=address,undefined)
endif()

catch_discover_tests(swish_utils_tests)
