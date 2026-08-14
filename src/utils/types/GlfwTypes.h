#pragma once

// Forward declaration only — consumers that just pass a window pointer around
// don't need the full GLFW header (and its platform includes).
struct GLFWwindow;

namespace swish {

// Named glfw_window (not window) to avoid colliding with swish::Window in core/.
using glfw_window = GLFWwindow;

}  // namespace swish
