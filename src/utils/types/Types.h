#pragma once

// ── Types.h ──────────────────────────────────────────────────────────
// Purpose   : umbrella for the project-wide type vocabulary, so most files need one include
//             instead of three. Narrow headers below are preferred in new library code.
// Units     : none (type aliases only). Namespace is plain `swish::` on purpose: u32/f32/Vec3
//             are vocabulary types used in ~600 places, not `util` functions.
// Complexity: compile-time only; no allocation, no code.
// Example   : #include "utils/types/Types.h"
//             swish::Vec3 p{0.f, 1.f, 0.f};  swish::u32 n = 3;
// See also  : types/GlmTypes.h (vec2/3/4, mat3/4, quat + legacy Vec3/Mat4 aliases),
//             types/StdTypes.h (u8..u64, i8..i64, f32/f64, usize),
//             types/GlfwTypes.h (glfw_window forward declaration, no GLFW include),
//             renderer/RendererConfig.h (MAX_FRAMES_IN_FLIGHT lives there, not here)
// Test      : tests/utils/test_types_Types.cpp

#include "GlfwTypes.h"  // IWYU pragma: export
#include "GlmTypes.h"   // IWYU pragma: export
#include "StdTypes.h"   // IWYU pragma: export
