#pragma once

#include "types/StdTypes.h"

// Kept as a macro for existing call sites; prefer swish::PI in new code.
#define SWISH_PI 3.14159265358979323846f

namespace swish {

static constexpr f32 PI = 3.14159265358979323846f;

// 1 scene-metre = 1000 world units (WU). Physics/geometry/lights are in WU.
static constexpr f32 WORLD_SCALE = 1000.0f;

static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

}  // namespace swish
