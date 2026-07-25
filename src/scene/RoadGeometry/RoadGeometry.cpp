#include "RoadGeometry.h"

#include <algorithm>
#include <cmath>

namespace swish {

RoadGeometry::RoadGeometry()  = default;
RoadGeometry::~RoadGeometry() = default;

float RoadGeometry::road_surface_y(float x) {
    if (x >= kEbServiceInner - 200.f && x <= kEbServiceOuter + 200.f)
        return kServiceY;
    float lanes_from_inner = (std::abs(x) - kBarrierRight) / kLaneWidth;
    float clamped          = std::max(0.f, std::min(kLaneCount, lanes_from_inner));
    return kCrownSlope * (kLaneCount - clamped) * kLaneWidth + kMarkingY;
}

void RoadGeometry::drivable_bounds(float x, double trueZ, float introLen, float& minX, float& maxX) {
    minX = kEbInnerBound;  // default EB (also the intro)
    maxX = kEbOuterBound;
    if (trueZ >= -static_cast<double>(introLen))
        return;
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

float RoadGeometry::ribbon_length(const Ribbon& r) {
    float len = 0.0f;
    for (size_t i = 1; i < r.pts.size(); ++i)
        len += glm::length(r.pts[i] - r.pts[i - 1]);
    return len;
}

void RoadGeometry::ribbon_sample(const Ribbon& r, float s, Vec3& pos, Vec3& tan) {
    if (r.pts.size() < 2) {
        pos = r.pts.empty() ? Vec3(0.0f) : r.pts[0];
        tan = Vec3(0.0f, 0.0f, -1.0f);
        return;
    }
    s = std::max(0.0f, s);
    for (size_t i = 1; i < r.pts.size(); ++i) {
        float seg = glm::length(r.pts[i] - r.pts[i - 1]);
        if (s <= seg || i == r.pts.size() - 1) {
            float u = (seg > 1e-4f) ? std::min(s / seg, 1.0f) : 0.0f;
            pos     = r.pts[i - 1] + (r.pts[i] - r.pts[i - 1]) * u;
            tan     = glm::normalize(r.pts[i] - r.pts[i - 1]);
            return;
        }
        s -= seg;
    }
}

}  // namespace swish
