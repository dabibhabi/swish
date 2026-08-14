#pragma once

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
