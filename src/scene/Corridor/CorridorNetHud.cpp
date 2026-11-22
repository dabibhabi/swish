// N9: the HUD strip and minimap feed for the Queens network (debug only at runtime; pure geometry, unit-tested).
#include "CorridorNetwork.h"
#include "scene/RoadScene/RoadParams.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace swish {

namespace {

namespace nw = road::network;
using util::units::m;

// Asset text fields are fixed char arrays that may fill to the last byte (no terminator).
template <size_t N>
std::string text(const char (&a)[N]) {
    return std::string(a, strnlen(a, N));
}

std::string places(std::string s) {
    for (size_t at; (at = s.find("; ")) != std::string::npos;) s.replace(at, 2, " \xC2\xB7 ");
    return s;
}

// Compaction undone: the road's real length over its baked one.
double real_k(const CorridorAlignment& al) {
    return al.header().realLength > 0.0 ? al.header().realLength / al.total_length() : 1.0;
}

std::string district(const CorridorAlignment& al, double s) {
    const CorridorDistrict* d = al.district_at(s);
    return d != nullptr ? text(d->name) : std::string();
}

}  // namespace

char net_direction(int r, int side) { return side > 0 ? nw::kHudRoads[r].up : nw::kHudRoads[r].down; }

NetHud network_hud(const CorridorNetwork& net, const NetPos& p, float roadEndsWu) {
    NetHud h;
    if (p.ramp >= 0) {
        const NetRamp& rp = net.ramps()[static_cast<size_t>(p.ramp)];
        h.road            = "Ramp";
        h.district        = district(net.road(rp.from != 255 ? rp.from : rp.to != 255 ? rp.to : 0), rp.endS[0]);
        // shortcut: a fork names its first live branch, not the one the car takes; pick by heading if owners mind.
        int    k    = p.ramp;
        double left = static_cast<double>(rp.end()) - p.s;
        for (int hop = 0; hop < nw::kHudRampHops && net.ramps()[static_cast<size_t>(k)].to == 255; ++hop) {
            const NetRampPoint& e    = net.ramps()[static_cast<size_t>(k)].pts.back();
            int                 next = -1;
            for (size_t j = 0; j < net.ramps().size(); ++j) {
                const NetRampPoint& f = net.ramps()[j].pts.front();
                if (static_cast<int>(j) != k && std::hypot(f.x - e.x, f.z - e.z) < m(nw::kWeld) &&
                    (next < 0 || !net.ramps()[j].closed))
                    next = static_cast<int>(j);
            }
            if (next < 0) break;
            k = next;
            left += static_cast<double>(net.ramps()[static_cast<size_t>(k)].end());
        }
        const NetRamp& last = net.ramps()[static_cast<size_t>(k)];
        h.dist              = std::max(0.0, left);
        if (rp.closed || last.closed || last.to == 255) {
            h.tab = "EXIT CLOSED", h.warn = true, h.dest = "Ramp closed ahead";
            return h;
        }
        const CorridorAlignment::Sample sm   = net.road(last.to).sample(last.endS[1]);
        const NetRampPoint&             tail = last.pts.back();
        const int                       side = tail.tx * sm.tx + tail.tz * sm.tz >= 0.0f ? +1 : -1;
        const nw::HudRoad&              to   = nw::kHudRoads[last.to];
        const std::string               dir(1, net_direction(last.to, side));
        h.tab  = "TO";
        h.top  = *to.shield ? std::string(to.shield) + " " + dir : std::string();
        h.dest = std::string(to.name) + (*to.shield ? "" : " " + dir);
        return h;
    }

    const CorridorAlignment& al = net.road(p.road);
    const nw::HudRoad&       R  = nw::kHudRoads[p.road];
    const double             k  = real_k(al);
    h.district                  = district(al, p.s);
    h.shield                    = R.shield;
    h.road                      = std::string(R.name) + " " + net_direction(p.road, p.side);

    double lo = 0.0, hi = 0.0;
    net.drivable(p.road, lo, hi);
    const CorridorExit* e  = nullptr;
    double              de = 1e300;
    for (const CorridorExit& x : al.exits()) {
        const double d = (x.station - p.s) * p.side;
        if ((x.westbound != 0) == (p.side < 0) && d > 0.0 && d < de && x.station >= lo && x.station <= hi)
            e = &x, de = d;
    }
    double dw = 1e300;  // the work-zone wall ahead (a data end with no wall merges into another road)
    for (const NetworkWall& w : net.asset().walls)
        if (w.road == p.road && (w.s - p.s) * p.side > 0.0) dw = std::min(dw, (w.s - p.s) * p.side);

    if (dw < de && dw * k <= static_cast<double>(roadEndsWu)) {
        h.tab = "ROAD ENDS", h.warn = true, h.dest = "Work zone \xC2\xB7 U-turn at the wall", h.dist = dw * k;
    } else if (e != nullptr) {
        const ExitSite x = net_exit_site(net, p.road, e->station, p.side);
        h.warn           = x.gore < 0.0 || x.closed;  // no ramp leaves there: closed too
        h.tab            = "EXIT " + text(e->ref) + (h.warn ? " CLOSED" : "");
        h.top            = text(e->destRef);
        h.dest           = places(text(e->dest));
        h.dist           = de * k;
    }
    return h;
}

std::vector<std::vector<Vec2>> network_map_lines(const CorridorNetwork& net) {
    std::vector<std::vector<Vec2>> out;
    for (int r = 1; r < static_cast<int>(net.road_count()); ++r) {
        const CorridorAlignment& al = net.road(r);
        double                   lo = 0.0, hi = 0.0;
        net.drivable(r, lo, hi);
        const double step = std::max(static_cast<double>(al.header().stationSpacing), (hi - lo) / 500.0);
        auto&        line = out.emplace_back();
        for (double s = lo;; s = std::min(s + step, hi)) {
            const auto sm = al.sample(s);
            line.emplace_back(static_cast<float>(sm.x), static_cast<float>(sm.z));
            if (s >= hi) break;
        }
    }
    for (const NetRamp& rp : net.ramps()) {
        auto& line = out.emplace_back();
        for (size_t i = 0; i < rp.pts.size(); i += nw::kHudMapEvery)
            line.emplace_back(static_cast<float>(rp.pts[i].x), static_cast<float>(rp.pts[i].z));
        line.emplace_back(static_cast<float>(rp.pts.back().x), static_cast<float>(rp.pts.back().z));
    }
    return out;
}

}  // namespace swish
