#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace swish {


using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;
using Quat = glm::quat;


static constexpr float WORLD_SCALE = 1000.0f;
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

}  // namespace swish
