#include "RoadGeometry.h"

#include "utils/geometry/Polyline.h"

#include <algorithm>
#include <cmath>

namespace swish {

RoadGeometry::RoadGeometry()  = default;
RoadGeometry::~RoadGeometry() = default;

bool  RoadGeometry::s_flatSurface  = false;
float RoadGeometry::s_flatSurfaceY = 0.0f;

void RoadGeometry::set_flat_surface(float y) {
    s_flatSurface  = true;
    s_flatSurfaceY = y;
}

void RoadGeometry::clear_flat_surface() { s_flatSurface = false; }

float RoadGeometry::road_surface_y(float x) {
    if (s_flatSurface) return s_flatSurfaceY;
    if (x >= kEbServiceInner - 200.f && x <= kEbServiceOuter + 200.f) return kServiceY;
    // Past EB mainline/shoulder edge: intro exit ramp + N Marginal (lifted asphalt).
    if (x >= kEbRoadRight - 50.f && x <= kIntroMarginalOuter + 200.f) return kIntroExitAsphaltY;
    float lanes_from_inner = (std::abs(x) - kBarrierRight) / kLaneWidth;
    float clamped          = std::max(0.f, std::min(kLaneCount, lanes_from_inner));
    return kCrownSlope * (kLaneCount - clamped) * kLaneWidth + kMarkingY;
}

void RoadGeometry::drivable_bounds(float x, double trueZ, float introLen, float& minX, float& maxX) {
    minX = kEbInnerBound;  // default EB (also the intro)
    maxX = kEbOuterBound;
    if (trueZ >= -static_cast<double>(introLen)) {
        // Intro: once past the exit deceleration station, open maxX onto N Marginal
        // so the car can steer right onto the diverge (Street View exit path).
        const float decelZ = intro_exit_decel_start_z(introLen);
        if (trueZ <= static_cast<double>(decelZ)) {
            maxX = kIntroMarginalOuter - 100.f;
            if (x >= kEbRoadRight - 500.f) {
                minX = kEbRoadRight - 200.f;
                maxX = kIntroMarginalOuter - 100.f;
            }
        }
        return;
    }
    // Endless region: is the car within its chunk's crossover window?
    double toEndless  = -static_cast<double>(introLen) - trueZ;  // ≥ 0
    int    k          = static_cast<int>(std::floor(toEndless / static_cast<double>(kChunkLen)));
    double localZ     = trueZ - (-static_cast<double>(introLen) - static_cast<double>(k) * kChunkLen);
    double xoverLocal = -0.5 * static_cast<double>(kChunkLen);
    double halfWindow = 45.0 * kFt + 30.0 * kFt;  // crossover half-length + merge margin
    if (x >= kEbServiceInner - 200.f) {           // on the EB frontage road (via the service ramp)
        minX = kEbServiceInner;
        maxX = kEbServiceOuter;
    } else if (std::abs(localZ - xoverLocal) < halfWindow) {
        minX = kWbOuterBound;  // open across the median
        maxX = kEbOuterBound;
    } else if (x < 0.0f) {
        minX = kWbOuterBound;  // committed to WB
        maxX = kWbInnerBound;
    }
}

float RoadGeometry::ribbon_length(const Ribbon& r) { return util::geometry::polyline_length(r.pts); }

void RoadGeometry::ribbon_sample(const Ribbon& r, float s, Vec3& pos, Vec3& tan) {
    util::geometry::polyline_sample(r.pts, s, pos, tan);
}

}  // namespace swish
