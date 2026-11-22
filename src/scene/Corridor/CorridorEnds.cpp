#include "CorridorEnds.h"

#include "scene/RoadScene/RoadParams.h"
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace swish {

using util::units::ft;
using util::units::m;
namespace E = road::ends;

CorridorEnds corridor_ends(const CorridorAlignment& al) {
    CorridorEnds e;
    const auto   x13 = std::find_if(al.exits().begin(), al.exits().end(),
                                    [](const CorridorExit& x) { return !x.westbound && std::atoi(x.ref) == 13; });
    if (!al.valid() || x13 == al.exits().end()) return e;
    const double P = x13->station - static_cast<double>(m(E::kPortalLead));
    e.portal       = P;
    e.plaza0       = P;
    e.plaza1       = P + static_cast<double>(m(E::kPlazaIn));
    e.plaza2       = e.plaza1 + static_cast<double>(m(E::kPlazaHold));
    e.plaza3       = e.plaza2 + static_cast<double>(m(E::kPlazaOut));
    e.plazaLanes   = E::kPlazaLanes;
    e.padW0        = P + static_cast<double>(m(E::kPadS));
    e.padW1        = e.padW0 + static_cast<double>(m(E::kPadLen));
    e.open0        = P + static_cast<double>(m(E::kOpenPad));
    e.ringRo       = m(E::kRingRo);
    e.ringRi       = m(E::kRingRi);
    e.ringC        = al.total_length() - static_cast<double>(m(E::kRingBack) + e.ringRo);
    e.barrierEnd   = e.ringC - static_cast<double>(e.ringRo + m(E::kSplitter));
    e.taper1       = e.barrierEnd - static_cast<double>(m(E::kTaperGap));
    e.taper0       = e.taper1 - static_cast<double>(ft(E::kTaperFt));
    // pavement runs on until its outer corner meets the ring's outer circle (ends not set yet: raw lanes, minus the
    // drop)
    const float edge = al.outer_edge(al.sample(e.taper1).lanes - 1.0f);
    e.mainEnd        = e.ringC - std::sqrt(static_cast<double>(e.ringRo * e.ringRo - edge * edge));
    return e;
}

std::vector<DriveSpan> ring_spans(const CorridorAlignment& al, double s) {
    const CorridorEnds& e = al.ends();
    const double        d = s - e.ringC;
    if (!al.has_ends() || std::abs(d) >= static_cast<double>(e.ringRo)) return {};
    const auto  half = [&](float r) { return static_cast<float>(std::sqrt(static_cast<double>(r * r) - d * d)); };
    const float wo   = half(e.ringRo);
    if (std::abs(d) >= static_cast<double>(e.ringRi)) return {{-wo, wo}};
    const float wi = half(e.ringRi);  // central island between the two windows
    return {{-wo, -wi}, {wi, wo}};
}

std::vector<SpotLightDesc> corridor_tunnel_lights(const CorridorAlignment& al, int side, double s0, double s1,
                                                  float lux) {
    std::vector<SpotLightDesc> out;
    const double               P = al.ends().portal;
    if (P <= 0.0) return out;
    const double sp = static_cast<double>(m(E::kLightSpacing));
    const float  h  = E::kCeilH - 0.15f;  // luminaire height above the road (m)
    // Road illuminance under one luminaire with its neighbours: E = I / h^2 * sum(cos^3)
    float sum = 0.0f;
    for (int k = -4; k <= 4; ++k) sum += std::pow(h / std::hypot(h, static_cast<float>(k) * E::kLightSpacing), 3.0f);
    const float cd  = lux * h * h / sum;
    const Vec3  col = util::light::kelvin_to_rgb(E::kLightK);
    for (double s = std::ceil(std::max(s0, static_cast<double>(m(E::kLightStart))) / sp) * sp; s < std::min(s1, P);
         s += sp) {
        const auto  sm    = al.sample(s);
        const float lat   = static_cast<float>(side) * 0.5f * (al.paint_inner(sm.lanes) + al.paint_outer(sm.lanes));
        const float d     = static_cast<float>(P - s) / m(1.0f);  // metres in from the portal
        float       boost = 1.0f;
        if (side < 0)  // entering: threshold zone, then a linear transition down to the interior level
            boost =
                d < E::kThreshLen
                    ? E::kThreshBoost
                    : std::max(1.0f, E::kThreshBoost - (E::kThreshBoost - 1.0f) * (d - E::kThreshLen) / E::kTransLen);
        else if (d < E::kExitLen)
            boost = E::kExitBoost;
        SpotLightDesc l{};
        l.position  = Vec3(static_cast<float>(sm.x) - sm.tz * lat, al.surface_y(s, lat) + m(h),
                           static_cast<float>(sm.z) + sm.tx * lat);
        l.direction = Vec3(0.0f, -1.0f, 0.0f);
        l.color     = col;
        l.intensity = cd * boost;
        l.range     = m(20.0f);
        l.cosInner  = std::cos(50.0f * util::units::kRadPerDeg);
        l.cosOuter  = std::cos(85.0f * util::units::kRadPerDeg);  // wide: the tiled walls must be lit too
        out.push_back(l);
    }
    return out;
}

std::vector<util::light::RoofVolume> corridor_tunnel_roofs(const CorridorAlignment& al, double s, double zShift) {
    std::vector<util::light::RoofVolume> out;
    const double                         P = al.ends().portal, L = static_cast<double>(m(E::kRoofChunk));
    if (P <= 0.0 || s > P + static_cast<double>(m(300.0f))) return out;
    const int n  = static_cast<int>(std::ceil(P / L));
    const int k0 = std::clamp(static_cast<int>(std::floor(s / L)) - util::light::kMaxRoofVolumes / 2, 0,
                              std::max(0, n - util::light::kMaxRoofVolumes));
    for (int k = k0; k < std::min(n, k0 + util::light::kMaxRoofVolumes); ++k) {
        const double a = static_cast<double>(k) * L, b = std::min(P, a + L);
        Vec3         lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const double st : {a, 0.5 * (a + b), b}) {
            const auto  sm = al.sample(st);
            const float w  = al.paint_outer(sm.lanes) + m(E::kWalkW + 0.3f);
            for (const float t : {-w, w}) {
                const float y = al.surface_y(st, t);
                const Vec3  p(static_cast<float>(sm.x) - sm.tz * t, y, static_cast<float>(sm.z + zShift) + sm.tx * t);
                lo = glm::min(lo, p - Vec3(0.0f, m(1.0f), 0.0f));
                hi = glm::max(hi, p + Vec3(0.0f, m(E::kCeilH + 0.3f), 0.0f));
            }
        }
        const auto sp     = al.sample(P);
        const bool portal = b >= P && sp.tz < 0.0f && std::abs(sp.tz) > std::abs(sp.tx);
        // portal chunk opens at its -Z end (the portal faces -Z here); interior chunks are closed rooms
        out.push_back({lo, hi, m(20.0f), portal ? 4 : -1, 0.002f});  // ~no sky light a tube length in
    }
    return out;
}

}  // namespace swish
