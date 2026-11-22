#pragma once

// LightingLab: a debug-only test world for understanding the lighting (tasks/todo.md "Lighting lab").
// The LIE corridor has no tunnel, no overpass and no lamps, so auto-exposure, sky occlusion, lumens and bloom
// could not be seen working. This is a walled, paved box about 2 miles on a side with one "station" per
// scenario. Lanes run along -Z (the car faces -Z at yaw 90):
//
//   start            x =    0   open pavement, free driving
//   unlit tunnel     x =  -60   400 m tube, no lamps: sky occlusion and exposure adapting down then up
//   lit tunnel       x = -100   400 m tube with ceiling lamps: lumens, pools, bloom in the dark
//   overpass series  x =  +60   decks 10, 20, 40 and 80 m wide, 150 m apart: short shadows
//   lamp stretch     x = +140   600 m with a street lamp every 40 m: the night road
//   calibration lot  x = +200   white / 18% grey / black cards, a matte and a metal ball, one lamp
//
// Car showcase places (docs/car-showcase.md), behind the start at +Z, lit by rectangle area lights:
//   studio           (-200, +200)  a closed dark room, one large soft panel above and a strip on each side
//   parking garage   (-200, +300)  low ceiling, pillars, rows of cool tube lights
//   gas canopy       (+260, +200)  a lit canopy on columns over two pump islands (built for night)
//   night street     (+260, +330)  buildings with lit shop windows and sodium lamps (built for night; R for rain)
//
// Pure geometry and numbers: no Vulkan. App::build_lab_scene uploads the result (SWISH_LAB=1, debug builds).
// Guide: docs/lighting-lab.md. Test: tests/test_lighting_lab.cpp.

#include "scene/SceneTypes.h"
#include "utils/light/SkyVisibility.h"

#include <array>
#include <span>
#include <vector>

namespace swish {

// Half the side of the paved box: 1600 m, so the box is 3.2 km (about 2 miles) across.
inline constexpr float kLabHalfSizeM = 1600.0f;
// The car is held this far inside the wall (a position clamp, no collision physics).
inline constexpr float kLabWallMarginM = 3.0f;

enum class LabStationId : uint8_t {
    Start,
    TunnelUnlit,
    TunnelLit,
    Overpasses,
    LampStretch,
    CalibrationLot,
    Studio,
    Garage,
    GasCanopy,
    NightStreet,
    Count
};

struct LabStation {
    const char* name;
    Vec3        position;        // WU, where a teleport puts the car
    float       yawDeg;          // car yaw; 90 faces -Z (down the lanes)
    int         timeOfDay = -1;  // TimeOfDay index a teleport switches to (-1 = leave it): night for the lit places
};

// One entry per LabStationId, in enum order.
[[nodiscard]] const std::array<LabStation, static_cast<size_t>(LabStationId::Count)>& lab_stations();

struct LightingLabWorld {
    MeshData                             mesh;
    std::vector<DrawCall>                draws;
    std::vector<LightDesc>               lights;  // intensity in candela, radius in WU
    std::vector<util::light::RoofVolume> roofs;   // at most util::light::kMaxRoofVolumes
    std::vector<RectLightDesc>           rects;   // area lights of the showcase places (nearest 16 are used)
};

// Build the whole lab. Deterministic.
[[nodiscard]] LightingLabWorld build_lighting_lab();

// Keep a position inside the wall (x and z clamped, y untouched).
[[nodiscard]] Vec3 lab_clamp_to_wall(const Vec3& positionWu);

// What the light meter would read on the ground under a point: the numbers behind the picture.
struct LabProbe {
    float skyVisibility = 1.0f;  // 1 = open sky, about 0 = deep under a roof
    float lampLux       = 0.0f;  // illuminance on the ground from all lamps in range (candela / d^2, windowed)
    int   lampsInRange  = 0;
    float nearestLampM  = -1.0f;  // metres to the nearest lamp, -1 if there is none
};
[[nodiscard]] LabProbe lab_probe(std::span<const LightDesc> lights, std::span<const util::light::RoofVolume> roofs,
                                 const Vec3& positionWu);

}  // namespace swish
