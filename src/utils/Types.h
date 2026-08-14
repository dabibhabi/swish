#pragma once

// Umbrella header — kept so existing includes of "utils/Types.h" keep working.
// New code can include the narrower header it actually needs:
//   utils/types/GlmTypes.h   — vec2/3/4, mat3/4, quat (+ legacy Vec3/Mat4/... aliases)
//   utils/types/StdTypes.h   — u32/f32/..., vector<T>, array<T,N>, unique_ptr<T>, str
//   utils/types/GlfwTypes.h  — glfw_window (forward-declared, no GLFW include)
//   utils/Constants.h        — PI, WORLD_SCALE, MAX_FRAMES_IN_FLIGHT

#include "Constants.h"        // IWYU pragma: export
#include "types/GlfwTypes.h"  // IWYU pragma: export
#include "types/GlmTypes.h"   // IWYU pragma: export
#include "types/StdTypes.h"   // IWYU pragma: export
