#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace swish {

// ── Math type aliases ──────────────────────────────────────────────
// TODO: Add more as needed (IVec2, UVec3, Mat3, etc.)
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;
using Quat = glm::quat;

// ── Scale constant ─────────────────────────────────────────────────
// 1 meter = 1000 world units (matches DownPour convention)
static constexpr float WORLD_SCALE = 1000.0f;

// ── Frames in flight ───────────────────────────────────────────────
// How many frames can be "in progress" at once (CPU records N+1 while GPU
// renders N)
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

}  // namespace swish
