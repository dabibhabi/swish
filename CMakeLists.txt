cmake_minimum_required(VERSION 3.20)
project(swish LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- GLFW (via Homebrew) ---
find_package(glfw3 3.3 REQUIRED)

# --- GLM (via Homebrew, header-only) ---
find_package(glm REQUIRED)

# --- macOS frameworks ---
find_library(COCOA_LIBRARY Cocoa REQUIRED)
find_library(IOKIT_LIBRARY IOKit REQUIRED)
find_library(OPENGL_LIBRARY OpenGL REQUIRED)

# --- Vulkan ---
find_package(Vulkan REQUIRED)

# --- GLAD (compiled as part of the project) ---
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/include)

# --- Main executable ---
add_executable(swish src/main.cpp src/core/Window/Window.cpp src/core/App/App.cpp src/renderer/VulkanContext/VulkanContext.cpp src/renderer/Renderer/Renderer.cpp)

target_include_directories(swish PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(swish PRIVATE
    glad
    glfw
    glm::glm
    ${OPENGL_LIBRARY}
    ${COCOA_LIBRARY}
    ${IOKIT_LIBRARY}
    Vulkan::Vulkan
)
