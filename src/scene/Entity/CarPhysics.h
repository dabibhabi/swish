#pragma once

// Pure vehicle-physics functions (SI units), independent of the renderer,
// GLFW, and GLM so they can be unit-tested without a GPU. CarEntity converts
// its world-unit state to/from SI at the call boundary.
//
// Umbrella header: the implementation is split by topic into the headers below
// (verbatim move of the former single-file CarPhysics.h, definition order
// preserved). Include order is the dependency order.
//
//   CarParams.h     CarParams, TireParams, kEnginePi
//   Powertrain.h    engine torque curve, GearboxParams, GearState, shift machine
//   Tire.h          per-axle load transfer + friction circle
//   Drivetrain.h    AWD PTU split + deliverable force
//   Longitudinal.h  brake/rear-traction caps, longitudinal_accel
//   Bicycle.h       dynamic bicycle model, yaw/sideslip limits, top speed
//   WetGrip.h       wet grip, hydroplaning, puddle mask

#include "Bicycle.h"
#include "CarParams.h"
#include "Drivetrain.h"
#include "Longitudinal.h"
#include "Powertrain.h"
#include "Tire.h"
#include "WetGrip.h"
