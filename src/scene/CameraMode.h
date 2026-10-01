#pragma once

#include "utils/meta/EnumCycle.h"

#include <cstdint>
#include <string_view>

namespace swish {

// Driving / debug camera presets. Cycle with C in App.
enum class CameraMode : uint8_t {
    Cockpit = 0,  // driver's seat (existing interior cam)
    Chase,        // classic third-person behind the car
    Hood,         // hood cam looking forward
    Bumper,       // low front bumper
    Roof,         // roof-mounted look-ahead
    Side,         // driver-side exterior
    Helicopter,   // high top-down for exits / corridor debug
    FreeFly,      // WASD + mouse (detached)
    Count
};

inline constexpr uint8_t camera_mode_count() {
    return util::meta::enum_count<CameraMode>();
}

inline CameraMode next_camera_mode(CameraMode m) {
    return util::meta::next_enum(m);
}

inline std::string_view camera_mode_name(CameraMode m) {
    switch (m) {
        case CameraMode::Cockpit:
            return "cockpit";
        case CameraMode::Chase:
            return "chase";
        case CameraMode::Hood:
            return "hood";
        case CameraMode::Bumper:
            return "bumper";
        case CameraMode::Roof:
            return "roof";
        case CameraMode::Side:
            return "side";
        case CameraMode::Helicopter:
            return "helicopter";
        case CameraMode::FreeFly:
            return "free-fly";
        default:
            return "unknown";
    }
}

// True when the eye is parented to the car (mouse look is relative to heading /
// look-at, not free FPS yaw).
inline bool camera_mode_car_attached(CameraMode m) {
    return m != CameraMode::FreeFly;
}

}  // namespace swish
