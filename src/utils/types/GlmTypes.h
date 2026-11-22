#pragma once

// ── GlmTypes.h ───────────────────────────────────────────────────────
// Purpose   : one place that pulls in GLM and names the vector/matrix types the engine uses.
// Units     : none; the meaning of a Vec3 (WU, metres, radians) is set by the call site.
// Complexity: compile-time only.
// Example   : swish::Vec3 p{0.f, 1.f, 0.f};  swish::Mat4 m = glm::translate(swish::Mat4(1.f), p);
// See also  : Types.h (umbrella). Legacy capitalised aliases (Vec3, Mat4, ...) are kept because
//             they have ~600 call sites; prefer the lowercase names in new code.
// Test      : tests/utils/test_types_Types.cpp

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace swish {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

using ivec2 = glm::ivec2;
using uvec2 = glm::uvec2;

using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;

// Legacy capitalized aliases — existing call sites use these; prefer the
// lowercase names above in new code.
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;
using Quat = glm::quat;

}  // namespace swish
