#include "CorridorRamps.h"

#include "CorridorAlignment.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "scene/RoadScene/RoadParams.h"
#include "utils/container/VectorOps.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace swish {

using util::container::filled;
using util::units::ft;

namespace {
// Service road inner edge: past the ramp's full offset plus a gap, as N Marginal in generate_exit_ramp.
float service_inner(const CorridorAlignment& al, double s) {
    return al.outer_edge(al.sample(s)) + ft(road::kExit.offset_ft + road::kExit.width_ft + road::kExit.marginal_gap_ft);
}
}  // namespace

CorridorPath corridor_exit_ramp(const CorridorAlignment& al, double s0, float dy) {
    const road::ExitRampParams& e = road::kExit;
    CorridorPath                p;
    p.s0        = s0;
    p.s1        = s0 + static_cast<double>(ft(e.decel_ft + e.diverge_ft + e.connect_ft));
    p.halfWidth = 0.5f * ft(e.width_ft);
    p.lat0      = al.outer_edge(al.sample(s0)) + p.halfWidth;  // inner edge flush with the shoulder
    p.lat1      = service_inner(al, p.s1) + 0.5f * ft(e.marginal_lane_ft);
    p.dy1       = dy;
    return p;
}

CorridorPath corridor_service_road(const CorridorAlignment& al, double s0, double s1, float dy) {
    const float  lane = ft(road::kExit.marginal_lane_ft);
    CorridorPath p;
    p.s0   = s0;
    p.s1   = s1;
    p.lat0 = p.lat1 = service_inner(al, s0) + lane;
    p.dy0 = p.dy1 = dy;
    p.halfWidth   = lane;
    p.centreLine  = true;
    return p;
}

CorridorPath corridor_crossover(const CorridorAlignment& al, double sCenter, double halfLen) {
    const CorridorHeader& h = al.header();
    CorridorPath          p;
    p.s0        = sCenter - halfLen;
    p.s1        = sCenter + halfLen;
    p.lat0      = h.medianHalfWidth + h.innerShoulder + 0.5f * h.laneWidth;
    p.lat1      = -p.lat0;
    p.halfWidth = 0.6f * h.laneWidth;
    return p;
}

PathPoint corridor_path_at(const CorridorAlignment& al, const CorridorPath& p, double s) {
    const double len = p.s1 - p.s0;
    const double u   = len > 0.0 ? std::clamp((s - p.s0) / len, 0.0, 1.0) : 0.0;
    const double hu  = std::clamp((u - p.h0) / std::max(1e-6, static_cast<double>(p.h1 - p.h0)), 0.0, 1.0);
    const float  e   = static_cast<float>(u * u * (3.0 - 2.0 * u));
    const float  eh  = static_cast<float>(hu * hu * (3.0 - 2.0 * hu));
    const float  de  = len > 0.0 ? static_cast<float>(6.0 * u * (1.0 - u) / len) : 0.0f;  // d(smoothstep)/ds
    const auto   sm  = al.sample(s);
    PathPoint    pt;
    pt.lat  = p.lat0 + (p.lat1 - p.lat0) * e;
    pt.dlat = (p.lat1 - p.lat0) * de;
    if (p.side != 0) {  // outward from the mainline outer edge on that side
        pt.lat  = static_cast<float>(p.side) * (al.outer_edge(sm) + pt.lat);
        pt.dlat = static_cast<float>(p.side) * pt.dlat;
    }
    // level on the plane's high edge: grass never pokes through
    const float plane = al.surface_y(s, pt.lat) + std::abs(sm.bank) * p.halfWidth;
    const float g     = p.g0 + (p.g1 - p.g0) * eh;
    pt.y              = plane + (al.ground_y(s) - plane) * g + p.dy0 + (p.dy1 - p.dy0) * eh;
    return pt;
}

float ramp_u_from_offset(const CorridorPath& p, float offset) {
    const double y = std::clamp(static_cast<double>((offset - p.lat0) / (p.lat1 - p.lat0)), 0.0, 1.0);
    return static_cast<float>(0.5 - std::sin(std::asin(1.0 - 2.0 * y) / 3.0));  // inverse of 3u^2 - 2u^3
}

std::vector<SweepFrame> corridor_path_frames(const CorridorAlignment& al, const CorridorPath& p, double from, double to,
                                             double ox, double oz) {
    const double a = std::max(from, p.s0), b = std::min(to, p.s1), len = p.s1 - p.s0;
    if (b - a < 1.0 || len <= 0.0) return {};
    const double sp = static_cast<double>(al.header().stationSpacing);
    const int    n  = std::max(2, static_cast<int>(std::ceil((b - a) / sp)) + 1);

    std::vector<Vec3> cl, tan;
    cl.reserve(n);
    tan.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double    s  = (i == n - 1) ? b : a + static_cast<double>(i) * sp;
        const PathPoint pt = corridor_path_at(al, p, s);
        const auto      sm = al.sample(s);
        const float     rx = -sm.tz, rz = sm.tx;  // right of travel
        cl.emplace_back(static_cast<float>(sm.x - ox + static_cast<double>(rx * pt.lat)), pt.y,
                        static_cast<float>(sm.z - oz + static_cast<double>(rz * pt.lat)));
        // sideways drift tilts the tangent, keeping the band square
        tan.emplace_back(sm.tx + rx * pt.dlat, 0.0f, sm.tz + rz * pt.dlat);
    }
    return build_sweep_frames(cl, tan);
}

float corridor_cross_half(const CorridorAlignment& al) {
    return 2.0f * al.header().laneWidth + util::units::m(diamond::kGutter);
}

std::vector<CorridorCrossroad> corridor_crossroads(const CorridorAlignment& al, float depth) {
    using util::units::m;
    struct Acc {
        double eb = 0.0, wb = 0.0;
        int    neb = 0, nwb = 0;
    };
    std::map<int, Acc> byRef;  // "49N", "49S" -> 49
    for (const CorridorExit& e : al.exits()) {
        const int n = std::atoi(e.ref);
        if (n <= 0) continue;
        Acc& a = byRef[n];
        (e.westbound ? a.wb : a.eb) += e.station;
        ++(e.westbound ? a.nwb : a.neb);
    }
    const double lead = static_cast<double>(m(diamond::kUnpairedLead));
    struct Raw {
        double c, ebDiv, wbDiv;
        int    no;
    };
    std::vector<Raw> raw;
    for (const auto& [n, a] : byRef) {
        const double eb = a.neb ? a.eb / a.neb : -1.0, wb = a.nwb ? a.wb / a.nwb : -1.0;
        const double c = (a.neb && a.nwb) ? 0.5 * (eb + wb) : a.neb ? eb + lead : wb - lead;
        raw.push_back({c, eb, wb, n});
    }
    std::sort(raw.begin(), raw.end(), [](const Raw& x, const Raw& y) { return x.c < y.c; });

    const double     margin  = static_cast<double>(al.header().turnaroundLength + m(diamond::kRampMax + 200.0f));
    const double     spacing = static_cast<double>(m(diamond::kMinSpacing));
    std::vector<Raw> kept;
    // C4: no diamond whose ramps would reach the tunnel or the plaza (open0 is 0 without route ends)
    const double lo       = std::max(margin, al.ends().open0 + static_cast<double>(m(diamond::kRampMax + 200.0f)));
    const auto   landmark = [](int n) {
        return std::find(std::begin(road::landmarks::kExits), std::end(road::landmarks::kExits), n) !=
               std::end(road::landmarks::kExits);
    };
    for (const Raw& r : raw) {
        if (r.c <= lo || r.c >= al.total_length() - margin) continue;
        if (kept.empty() || r.c - kept.back().c >= spacing)
            kept.push_back(r);
        else if (landmark(r.no) && !landmark(kept.back().no))
            kept.back() = r;  // D6: a landmark exit keeps its diamond over a closer plain neighbour
    }

    const double join = static_cast<double>(m(diamond::kJoin)), rmin = static_cast<double>(m(diamond::kRampMin));
    const double rmax = static_cast<double>(m(diamond::kRampMax)), pad = static_cast<double>(m(20.0f));
    const float  need = m(diamond::kDeck) + util::units::ft(diamond::kClearFt) + m(0.1f);
    std::vector<CorridorCrossroad> out;
    for (size_t i = 0; i < kept.size(); ++i) {
        const Raw& r = kept[i];
        // ramps stop short of halfway to the neighbour, so neighbouring diamonds never overlap
        const double      maxB = std::min(rmax, i ? 0.5 * (r.c - kept[i - 1].c) - join - pad : rmax);
        const double      maxA = std::min(rmax, i + 1 < kept.size() ? 0.5 * (kept[i + 1].c - r.c) - join - pad : rmax);
        const double      lb   = r.ebDiv >= 0.0 ? r.c - join - r.ebDiv : 0.5 * (rmin + rmax);
        const double      la   = r.wbDiv >= 0.0 ? r.wbDiv - r.c - join : 0.5 * (rmin + rmax);
        const auto        sm   = al.sample(r.c);
        CorridorCrossroad x;
        x.s         = r.c;
        x.depth     = std::max(depth, need + std::abs(sm.bank) * al.outer_edge(sm));
        x.lenBefore = static_cast<float>(std::clamp(lb, rmin, std::max(rmin, maxB)));
        x.lenAfter  = static_cast<float>(std::clamp(la, rmin, std::max(rmin, maxA)));
        x.exitNo    = r.no;
        out.push_back(x);
    }
    return out;
}

namespace {
struct Piece {
    double a, b;
    int    lanes;
};
// pieces minus [a, b]
void cut(std::vector<Piece>& v, double a, double b) {
    std::vector<Piece> out;
    for (const Piece& p : v) {
        if (b <= p.a || a >= p.b) {
            out.push_back(p);
            continue;
        }
        if (a > p.a) out.push_back({p.a, a, p.lanes});
        if (b < p.b) out.push_back({b, p.b, p.lanes});
    }
    v.swap(out);
}
}  // namespace

std::vector<CorridorPath> corridor_diamond_paths(const CorridorAlignment& al) {
    using util::units::ft;
    using util::units::m;
    std::vector<CorridorPath> out;
    const float  lane = al.header().laneWidth, S = al.sr_strip(), cw = m(diamond::kCurbW), gut = m(diamond::kGutter);
    const double join = static_cast<double>(m(diamond::kJoin)), stub = static_cast<double>(m(diamond::kStubPad));
    // less 0.5 m so the service road tucks under the crossroad (no crack)
    const double crossHalf = static_cast<double>(corridor_cross_half(al) - m(0.5f));
    const float  hw        = 0.5f * ft(diamond::kRampWidthFt);

    for (const int side : {+1, -1}) {
        std::vector<Piece> pieces;
        for (const CorridorServiceRoad& r : al.service_roads())
            if ((r.westbound != 0) == (side < 0)) pieces.push_back({r.s0, r.s1, std::max<int>(1, r.lanes)});
        // stubs where no service road carries both ramp landings
        for (const CorridorCrossroad& c : al.crossroads()) {
            // the last 40 % of each ramp runs on the service road
            const double lo = c.s - join - 0.4 * static_cast<double>(c.lenBefore);
            const double hi = c.s + join + 0.4 * static_cast<double>(c.lenAfter);
            const bool   ok =
                std::any_of(pieces.begin(), pieces.end(), [&](const Piece& p) { return p.a <= lo && p.b >= hi; });
            if (ok) continue;
            std::vector<Piece> st{{c.s - join - stub, c.s + join + stub, 2}};
            for (const Piece& p : pieces) cut(st, p.a, p.b);
            pieces.insert(pieces.end(), st.begin(), st.end());
        }
        for (const CorridorCrossroad& c : al.crossroads()) cut(pieces, c.s - crossHalf, c.s + crossHalf);
        if (al.has_ends()) {  // C4: service roads start past the plaza and stop before the lane drop
            cut(pieces, 0.0, al.ends().open0);
            cut(pieces, al.ends().taper0, al.total_length());
        }
        for (const Piece& p : pieces) {
            if (p.b - p.a < static_cast<double>(m(20.0f))) continue;
            CorridorPath sr;
            sr.kind      = CorridorPath::Kind::Service;
            sr.side      = side;
            sr.lanes     = p.lanes;
            sr.s0        = p.a;
            sr.s1        = p.b;
            sr.halfWidth = 0.5f * (static_cast<float>(p.lanes) * lane) + gut;
            sr.lat0 = sr.lat1 = S + cw + sr.halfWidth;
            sr.g0 = sr.g1 = 1.0f;
            out.push_back(sr);
        }

        // Ramps. "Before" the crossroad (lower s) the mainline end is s0: EB exit, WB entrance.
        const float oMain = hw - al.header().outerShoulder;  // inner edge on the mainline edge line
        const float oSr   = S + cw + gut + hw;               // inner edge on the service road's inner edge line
        for (const CorridorCrossroad& c : al.crossroads()) {
            for (const bool before : {true, false}) {
                CorridorPath r;
                r.kind      = CorridorPath::Kind::Ramp;
                r.side      = side;
                r.halfWidth = hw;
                r.dy0 = r.dy1 = diamond::kLiftWu;  // coplanar with the shoulder / service road at the ends
                r.s0          = before ? c.s - join - c.lenBefore : c.s + join;
                r.s1          = before ? c.s - join : c.s + join + c.lenAfter;
                r.lat0        = before ? oMain : oSr;
                r.lat1        = before ? oSr : oMain;
                r.g0          = before ? 0.0f : 1.0f;
                r.g1          = 1.0f - r.g0;
                // height eases only while the ramp is free of both shoulder and service road
                const float uNose  = ramp_u_from_offset(r, hw);
                const float uTouch = ramp_u_from_offset(r, S - hw);
                r.h0               = std::min(uNose, uTouch);
                r.h1               = std::max(uNose, uTouch);
                out.push_back(r);
            }
        }
    }
    return out;
}

// ── C3a ──────────────────────────────────────────────────────────────
float crossroad_sr_outer(const CorridorAlignment& al, double cs, const std::vector<CorridorPath>& paths, int side) {
    using util::units::m;
    const double tuck = static_cast<double>(corridor_cross_half(al) - m(0.5f)), near = static_cast<double>(m(1.0f));
    const float  in = al.outer_edge(al.sample(cs)) + al.sr_strip() + m(diamond::kCurbW);
    for (const CorridorPath& p : paths)
        if (p.kind == CorridorPath::Kind::Service && p.side == side &&
            (std::abs(p.s1 - (cs - tuck)) <= near || std::abs(p.s0 - (cs + tuck)) <= near))
            return in + 2.0f * p.halfWidth;
    return 0.0f;
}

std::vector<CrossStub> corridor_cross_stubs(const CorridorAlignment& al, const std::vector<CorridorPath>& paths) {
    std::vector<CrossStub> out;
    if (al.cross_stub() <= 0.0f) return out;
    for (const CorridorCrossroad& c : al.crossroads())
        for (const int side : {+1, -1})
            if (const float o = crossroad_sr_outer(al, c.s, paths, side); o > 0.0f)
                out.push_back({c.s, side, o, o + al.cross_stub()});
    return out;
}

bool signal_lit(int group, int colour, double t, float cycle) {
    const double c = std::max(static_cast<double>(cycle), 2.0 * (diamond::kAmberS + diamond::kAllRedS) + 2.0);
    const double h = 0.5 * c, p = std::fmod(t + (group ? h : 0.0), c);
    const int    lit = p < h - diamond::kAmberS - diamond::kAllRedS ? 2 : p < h - diamond::kAllRedS ? 1 : 0;
    return lit == colour;
}

Vec4 signal_lens_color(int colour, bool lit) {
    // LED red / amber / blue-green; an unlit lens is a dark tinted dome
    static constexpr Vec4 kTint[3] = {
        {1.0f, 0.10f, 0.05f, 1.0f}, {1.0f, 0.55f, 0.05f, 1.0f}, {0.10f, 1.0f, 0.70f, 1.0f}};
    const Vec4 c = kTint[std::clamp(colour, 0, 2)];
    return lit ? c : Vec4(Vec3(c) * 0.08f, 1.0f);
}

float signal_lens_nits(int colour) {
    // ITE VTCSH minimum on-axis intensity (365 / 910 / 475 cd) over a 12 in lens (0.073 m^2)
    static constexpr float kNits[3] = {5000.0f, 12500.0f, 6500.0f};
    return kNits[std::clamp(colour, 0, 2)];
}

CorridorHov corridor_hov(const CorridorAlignment& al) {
    CorridorHov h;
    double      a = -1.0, b = -1.0;
    for (const CorridorExit& e : al.exits()) {
        const int n = std::atoi(e.ref);
        if (n == diamond::kHovFromExit) a = a < 0.0 ? e.station : std::min(a, e.station);
        if (n == diamond::kHovToExit) b = std::max(b, e.station);
    }
    if (a < 0.0 || b <= a) return h;
    h.s0              = a;
    h.s1              = b;
    const double len  = static_cast<double>(ft(diamond::kHovZoneFt)),
                 lead = static_cast<double>(ft(diamond::kHovZoneLeadFt));
    for (const CorridorExit& e : al.exits()) {
        if (e.station <= a || e.station >= b) continue;
        const int    w  = e.westbound ? 1 : 0;
        const double z0 = w ? e.station + lead : e.station - lead - len;  // upstream in travel direction
        const double lo = std::max(a, z0), hi = std::min(b, z0 + len);
        if (hi - lo > 1000.0) h.zones[w].push_back({lo, hi});
    }
    for (auto& z : h.zones) {  // sort + merge overlaps (paired ramps can land close)
        std::sort(z.begin(), z.end());
        std::vector<std::pair<double, double>> m;
        for (const auto& k : z)
            if (!m.empty() && k.first <= m.back().second)
                m.back().second = std::max(m.back().second, k.second);
            else
                m.push_back(k);
        z.swap(m);
    }
    const double sp = static_cast<double>(ft(diamond::kDiamondSpacingFt));
    for (double s = a + sp; s < b; s += sp) h.diamonds[0].push_back(s);
    for (double s = b - sp; s > a; s -= sp) h.diamonds[1].push_back(s);
    return h;
}

void emit_corridor_path(MeshBuilder& b, const std::vector<SweepFrame>& f, const CorridorPath& p, const Vec4& asphalt,
                        const Vec4& white, const Vec4& yellow, float tile) {
    const size_t n  = f.size();
    const float  hw = p.halfWidth, lw = ft(0.5f), my = road::kLie.marking_y;
    b.emit_sweep_band(f, filled(n, -hw), filled(n, hw), 0.0f, asphalt, MAT_ASPHALT, tile);
    b.emit_sweep_band(f, filled(n, -hw), filled(n, lw - hw), my, white);
    b.emit_sweep_band(f, filled(n, hw - lw), filled(n, hw), my, white);
    if (!p.centreLine) return;
    const float g = ft(0.1f);  // gap between the two yellow lines, as generate_service_roads
    b.emit_sweep_band(f, filled(n, -g - 0.5f * lw), filled(n, -g), my, yellow);
    b.emit_sweep_band(f, filled(n, g), filled(n, g + 0.5f * lw), my, yellow);
}

}  // namespace swish
