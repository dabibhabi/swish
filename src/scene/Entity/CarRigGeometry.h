#pragma once

// CarRigGeometry: wheel geometry measured by the loader, shared by the rig loader (CarRig.h,
// which fills it) and the physics profiles (CarProfile.h, which consume it). Plain data with
// no GLM or renderer dependency, so the physics side can include it.

namespace swish {

// Geometry measured from the four hub pivots after normalization (metres). For any
// car whose wheel frames were recovered; named_rig says they came from named pivots.
struct CarRigGeometry {
    float wheelbase_m         = 0.f;  // front axle centre to rear axle centre, along the nose
    float track_front_m       = 0.f;  // FL to FR hub, across the car
    float track_rear_m        = 0.f;
    float tyre_radius_front_m = 0.f;  // mean grounded hub height of the axle
    float tyre_radius_rear_m  = 0.f;
    bool  named_rig           = false;
};

}  // namespace swish
