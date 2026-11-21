#pragma once

// Car lights (tasks/todo.md "Car lights"): headlight / tail / brake anchors read from
// the car GLB by exact node name, and the pure function that turns them into this
// frame's world-space spot lights. Header-only and GPU/GLFW-free so it is unit-tested
// in tests/test_car_lights.cpp.
//
// Asset convention (docs/car-asset-pipeline.md, Blender rig `porsche_rigged.blend`):
// each light is a pivot Empty (Headlight_L, TailLight_R, ...) holding its lens and
// housing meshes plus a `<Anchor>_Lamp` KHR_lights_punctual node. The exporter
// orients the lamp node so its local -Z is the beam direction (the Empty itself is
// not, after the Y-up axis swap), so the loader reads the `_Lamp` nodes.

#include "scene/SceneTypes.h"  // SpotLightDesc
#include "utils/light/Photometry.h"
#include "utils/types/Types.h"
#include "utils/units/Units.h"

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace swish {

enum class CarLightKind : uint8_t { Headlight, TailLight, BrakeLight };

// One lamp in car space (the normalized, grounded mesh space ModelManager emits:
// metres, nose +X, up +Y). Multiplied by the car's model matrix each frame.
struct CarLightAnchor {
    CarLightKind kind;
    Vec3         position;
    Vec3         direction;  // unit
};

// What is switched on this frame.
struct CarLightState {
    bool  headlights = false;
    bool  tail       = false;
    float brake      = 0.f;  // [0,1] brake pedal
};

// Look of each lamp kind. Ranges in metres (converted to WU here); angles are
// half-angles in degrees. Defaults are the debug-panel defaults (Tunables.def
// group "car_lights"). Flux is in lumens (typical figures, check a datasheet); car_spot_lights converts to
// candela with the Frostbite rule I = flux / pi. For a headlight the cone rule
// util::light::cone_candela_from_lumens is the more physical one: a tighter beam is brighter.
struct CarLightParams {
    Vec3  headColor{1.0f, 0.94f, 0.82f};
    float headLumens   = 1200.0f;  // one low beam, about 1,000 to 1,500 lm
    float headRangeM   = 60.0f;
    float headInnerDeg = 12.0f;
    float headOuterDeg = 28.0f;
    bool  headCutoff   = false;  // low-beam horizontal cutoff (lighting L6.4)
    bool  headConeRule = false;  // candela from the cone's solid angle (physical) instead of flux / pi

    Vec3  tailColor{1.0f, 0.05f, 0.02f};
    float tailLumens   = 20.0f;   // tail lamp, a few candela
    float brakeLumens  = 300.0f;  // stop lamp, tens of candela
    float tailRangeM   = 6.0f;
    float tailOuterDeg = 70.0f;
};

// Node name -> lamp kind, exact match on the rig's `_Lamp` nodes only.
inline std::optional<CarLightKind> car_light_kind_from_node(std::string_view name) {
    if (name == "Headlight_L_Lamp" || name == "Headlight_R_Lamp") return CarLightKind::Headlight;
    if (name == "TailLight_L_Lamp" || name == "TailLight_R_Lamp" || name == "RearLightBar_Lamp")
        return CarLightKind::TailLight;
    if (name == "BrakeLight_L_Lamp" || name == "BrakeLight_R_Lamp") return CarLightKind::BrakeLight;
    return std::nullopt;
}

// Anchors (car space) + car model matrix (car space -> world, incl. the 1000 WU/m
// entity scale) -> this frame's world spot lights. Lamps that are off are omitted,
// so an all-off state returns an empty list (identity: no light added).
inline std::vector<SpotLightDesc> car_spot_lights(const std::vector<CarLightAnchor>& anchors, const Mat4& model,
                                                  const CarLightState& state, const CarLightParams& p) {
    std::vector<SpotLightDesc> out;
    out.reserve(anchors.size());
    const glm::mat3 basis(model);
    const auto      cosDeg = [](float deg) { return std::cos(glm::radians(deg)); };

    for (const CarLightAnchor& a : anchors) {
        SpotLightDesc s{};
        s.position  = Vec3(model * Vec4(a.position, 1.f));
        s.direction = glm::normalize(basis * a.direction);
        switch (a.kind) {
            case CarLightKind::Headlight:
                if (!state.headlights) continue;
                s.color     = p.headColor;
                s.intensity = p.headConeRule
                                  ? util::light::cone_candela_from_lumens(p.headLumens, glm::radians(p.headOuterDeg))
                                  : util::light::spot_candela_from_lumens(p.headLumens);
                s.range     = util::units::m(p.headRangeM);
                s.cosInner  = cosDeg(p.headInnerDeg);
                s.cosOuter  = cosDeg(p.headOuterDeg);
                s.cutoff    = p.headCutoff;
                break;
            case CarLightKind::TailLight:
                if (!state.tail) continue;
                s.color     = p.tailColor;
                s.intensity = util::light::spot_candela_from_lumens(p.tailLumens);
                s.range     = util::units::m(p.tailRangeM);
                s.cosInner  = cosDeg(p.tailOuterDeg * 0.5f);
                s.cosOuter  = cosDeg(p.tailOuterDeg);
                break;
            case CarLightKind::BrakeLight:
                if (state.brake <= 0.f) continue;
                s.color     = p.tailColor;
                s.intensity = util::light::spot_candela_from_lumens(p.brakeLumens) * state.brake;
                s.range     = util::units::m(p.tailRangeM);
                s.cosInner  = cosDeg(p.tailOuterDeg * 0.5f);
                s.cosOuter  = cosDeg(p.tailOuterDeg);
                break;
        }
        if (s.intensity <= 0.f) continue;
        out.push_back(s);
    }
    return out;
}

}  // namespace swish
