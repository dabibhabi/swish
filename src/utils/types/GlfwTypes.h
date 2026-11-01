#pragma once

// ── GlfwTypes.h ──────────────────────────────────────────────────────
// Purpose   : forward-declare the GLFW window type so headers that only pass a window pointer
//             around do not include <GLFW/glfw3.h> (keeps utils and tests GLFW-free).
// Units     : none.
// Complexity: compile-time only.
// Example   : void attach(swish::glfw_window* w);   // no GLFW include needed in the header
// See also  : Types.h (umbrella)
// Test      : tests/utils/test_types_Types.cpp

// Forward declaration only — consumers that just pass a window pointer around
// don't need the full GLFW header (and its platform includes).
struct GLFWwindow;

namespace swish {

// Named glfw_window (not window) to avoid colliding with swish::Window in core/.
using glfw_window = GLFWwindow;

}  // namespace swish
