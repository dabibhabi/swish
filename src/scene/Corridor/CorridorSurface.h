#pragma once

#include "CorridorRamps.h"

#include <cstdint>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// C3 drivable surface: the union of the mainline carriageways (with the dip),
// ramps, service roads and the service road's run across each crossroad.
// Pure functions of the alignment and the diamond paths (CorridorBounds.cpp),
// so the unit tests reach them without Vulkan or GLFW.
// ══════════════════════════════════════════════════════════════════════
enum class DrivePiece : uint8_t { None, Mainline, Ramp, Service, Crossroad };

struct DriveSurface {
    DrivePiece piece = DrivePiece::None;
    float      y     = 0.0f;  // surface height (WU)
    float      dyds  = 0.0f;  // grade along s; off the mainline every piece is level across
};

// One allowed lateral window for the car centre (WU, lo <= hi).
struct DriveSpan {
    float lo = 0.0f, hi = 0.0f;
};

// Highest piece under (s, lat); piece None (mainline height) when nothing covers it.
DriveSurface drive_surface(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double s, float lat);

// Merged lateral windows at s for a car committed to `side`, each piece shrunk by `inset` per edge.
// Mainline only (paths empty) gives exactly CorridorDriver::carriageway_bounds.
std::vector<DriveSpan> drive_spans(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double s,
                                   int side, float inset);

}  // namespace swish
