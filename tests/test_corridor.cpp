#include "scene/Corridor/CorridorAlignment.h"
#include "scene/Corridor/CorridorDriver.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "utils/units/Units.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace swish;
using Catch::Approx;

// ══════════════════════════════════════════════════════════════════════
// Phase 3a — real LIE corridor.
//
// Two things get tested here, both flagged by the phase doc as the parts that
// had to be got right before anything is built on top of them:
//
//   1. the coordinate transform (§4.1) — reimplemented independently and
//      checked against known real-world distances;
//   2. the compaction heuristic (§4.2) — its invariants, including the
//      curvature floor that stage 3a added after measuring what uniform
//      compression did to real curves.
//
// Plus the corridor-frame alignment queries and drivable bounds, which are
// what replaced the retired treadmill's world-X clamp.
// ══════════════════════════════════════════════════════════════════════

namespace {

constexpr double kWorldScale = swish::util::units::kWuPerMetre;  // 1 m = 1000 WU (Units.h)
constexpr double kEarthR     = 6378137.0;
constexpr double kMile       = 1609.344;

// ── 1. The coordinate transform, reimplemented from the spec ─────────
// Independent restatement of tools/lie_corridor.py::TangentPlane, so a change
// to one and not the other is caught rather than silently agreed with.
struct Plane {
    double lat0, lon0, psi;

    void to_plane(double lon, double lat, double& e, double& n) const {
        e = (lon - lon0) * kEarthR * std::cos(lat0 * M_PI / 180.0) * M_PI / 180.0;
        n = (lat - lat0) * kEarthR * M_PI / 180.0;
    }
    // -> engine (x, z) in metres. x = -n', z = -e' after rotating by -psi:
    // travel east maps to -Z and geographic south (right of eastbound travel)
    // maps to +X, matching cross((0,0,-1),(0,1,0)) = (1,0,0).
    void to_engine(double lon, double lat, double& x, double& z) const {
        double e, n;
        to_plane(lon, lat, e, n);
        const double c = std::cos(psi), s = std::sin(psi);
        x = -(-e * s + n * c);
        z = -(e * c + n * s);
    }
};

// The corridor's own tangent-plane origin, from the baked asset's provenance
// fields (route midpoint of the real I-495 chain).
Plane lie_plane(double psi = 0.0) { return Plane{40.762599, -73.721674, psi}; }

// Build a synthetic corridor asset on disk: a straight run along -Z with a
// lane-count taper in the middle. Lets the alignment/driver tests exercise the
// real loader and real header without depending on the baked 18 mi asset.
std::string write_synthetic(const char* name, int nStations, float spacing, float lanesA, float lanesB) {
    CorridorHeader h{};
    h.magic            = CorridorHeader::MAGIC;
    h.version          = CorridorHeader::VERSION;
    h.stationCount     = static_cast<uint32_t>(nStations);
    h.landmarkCount    = 1;
    h.stationSpacing   = spacing;
    h.segmentLength    = 1200000.0f;
    h.tileLength       = 300000.0f;
    h.laneWidth        = 3657.6f;
    h.medianHalfWidth  = 381.0f;
    h.innerShoulder    = 1219.2f;
    h.outerShoulder    = 3048.0f;
    h.barrierWidth     = 762.0f;
    h.barrierHeight    = 862.584f;
    h.grassExtent      = 36576.0f;
    h.edgeLine         = 152.4f;
    h.dashLength       = 3048.0f;
    h.dashGap          = 9144.0f;
    h.roadY            = 6.0f;
    h.grassY           = 0.0f;
    h.markingLift      = 5.0f;
    h.turnaroundLength = 220000.0f;
    h.startStation     = 340000.0f;
    h.compactionRadius = 900000.0f;
    h.compactionBlend  = 250000.0f;
    h.compactionFactor = 0.1854f;
    h.chordBearingDeg  = 5.282f;
    h.totalLength      = static_cast<double>(nStations - 1) * spacing;
    h.realLength       = h.totalLength * 1.734;
    h.originLat        = 40.762599;
    h.originLon        = -73.721674;

    std::vector<CorridorStation> st(nStations);
    for (int i = 0; i < nStations; ++i) {
        const float u = (nStations > 1) ? static_cast<float>(i) / static_cast<float>(nStations - 1) : 0.0f;
        st[i].x       = 0.0;
        st[i].z       = -static_cast<double>(i) * spacing;
        st[i].tx      = 0.0f;
        st[i].tz      = -1.0f;
        st[i].lanes   = lanesA + (lanesB - lanesA) * u;
    }
    CorridorLandmark lm{};
    lm.station = 0.5 * h.totalLength;
    std::snprintf(lm.ref, sizeof(lm.ref), "38");

    const std::string path = std::string("./") + name;
    FILE*             f    = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(&h, sizeof(h), 1, f);
    std::fwrite(st.data(), sizeof(CorridorStation), st.size(), f);
    std::fwrite(&lm, sizeof(lm), 1, f);
    std::fclose(f);
    return path;
}

// ── 2. The compaction heuristic, reimplemented from the spec ─────────
double smoothstep(double t) {
    t = std::max(0.0, std::min(1.0, t));
    return t * t * (3.0 - 2.0 * t);
}

double landmark_weight(double s, const std::vector<double>& marks, double N, double W) {
    double d = 1e300;
    for (double m : marks) d = std::min(d, std::abs(s - m));
    if (d <= N) return 1.0;
    if (d >= N + W) return 0.0;
    return smoothstep(1.0 - (d - N) / W);
}

// f_i = max(w_i + c(1-w_i), min(1, R_floor/R_i))
double scale_at(double w, double radius, double c, double floorR) {
    const double g = (radius > 1e-9) ? std::min(1.0, floorR / radius) : 1.0;
    return std::max(w + c * (1.0 - w), g);
}

double total_length(const std::vector<double>& w, const std::vector<double>& radius, double step, double c,
                    double floorR) {
    double t = 0.0;
    for (size_t i = 0; i < w.size(); ++i) t += step * scale_at(w[i], radius[i], c, floorR);
    return t;
}

double solve_c(const std::vector<double>& w, const std::vector<double>& radius, double step, double target,
               double floorR) {
    double lo = 0.0, hi = 1.0;
    if (total_length(w, radius, step, hi, floorR) <= target) return 1.0;
    if (total_length(w, radius, step, lo, floorR) >= target) return 0.0;
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (total_length(w, radius, step, mid, floorR) < target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════
// Coordinate transform (spec §4.1)
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("tangent plane: the origin maps to the engine origin", "[corridor][transform]") {
    const Plane p = lie_plane();
    double      x = 1.0, z = 1.0;
    p.to_engine(p.lon0, p.lat0, x, z);
    CHECK(x == Approx(0.0).margin(1e-9));
    CHECK(z == Approx(0.0).margin(1e-9));
}

TEST_CASE("tangent plane: one degree of latitude is ~111.2 km", "[corridor][transform]") {
    // Ground truth: a degree of latitude on the WGS84 sphere used here is
    // pi*R/180 = 111.320 km. This pins the north scale factor.
    const Plane p = lie_plane();
    double      e = 0.0, n = 0.0;
    p.to_plane(p.lon0, p.lat0 + 1.0, e, n);
    CHECK(e == Approx(0.0).margin(1e-6));
    CHECK(n / 1000.0 == Approx(111.320).epsilon(0.001));
}

TEST_CASE("tangent plane: longitude scale shrinks by cos(latitude)", "[corridor][transform]") {
    // At 40.76 N a degree of longitude is 111.320 * cos(40.7626) = 84.4 km.
    const Plane p = lie_plane();
    double      e = 0.0, n = 0.0;
    p.to_plane(p.lon0 + 1.0, p.lat0, e, n);
    CHECK(n == Approx(0.0).margin(1e-6));
    CHECK(e / 1000.0 == Approx(111.320 * std::cos(40.762599 * M_PI / 180.0)).epsilon(0.001));
}

TEST_CASE("tangent plane: real LIE end-to-end distance is right", "[corridor][transform]") {
    // Manhattan portal (-73.9747, 40.7460) to just past exit 49 (-73.4180,
    // 40.7878) — both taken from the checked-in OSM extract. Great-circle
    // distance between them is ~47.3 km; the corridor's ARC length is longer
    // (31.20 mi = 50.2 km) because the road is not a straight line. The chord
    // is what this transform must reproduce.
    const Plane p = lie_plane();
    double      x0, z0, x1, z1;
    p.to_engine(-73.9747, 40.7460, x0, z0);
    p.to_engine(-73.4180, 40.7878, x1, z1);
    const double chord = std::hypot(x1 - x0, z1 - z0);
    CHECK(chord / 1000.0 == Approx(47.3).epsilon(0.02));
    CHECK(chord < 31.20 * kMile);  // chord must be shorter than the arc
}

TEST_CASE("tangent plane: east maps to -Z and south maps to +X", "[corridor][transform]") {
    // The engine drives along -Z with +X to the driver's right
    // (cross((0,0,-1),(0,1,0)) = (1,0,0)); driving east, the right-hand side is
    // south. With psi = 0 (chord already due east) that fixes the whole map.
    const Plane p = lie_plane(0.0);
    double      x, z;
    p.to_engine(p.lon0 + 0.01, p.lat0, x, z);  // due EAST
    CHECK(z < 0.0);
    CHECK(x == Approx(0.0).margin(1e-6));

    p.to_engine(p.lon0, p.lat0 - 0.01, x, z);  // due SOUTH
    CHECK(x > 0.0);
    CHECK(z == Approx(0.0).margin(1e-6));
}

TEST_CASE("tangent plane: the rotation is a rigid motion (lengths preserved)", "[corridor][transform]") {
    // Whatever chord bearing is baked in, the map must not scale or shear —
    // only rotate and flip handedness. Distance between two fixed points is
    // therefore independent of psi.
    const double a[2] = {-73.90, 40.74};
    const double b[2] = {-73.50, 40.79};
    double       ref  = 0.0;
    for (double psiDeg : {0.0, 5.282, 37.0, -90.0, 180.0}) {
        const Plane p = lie_plane(psiDeg * M_PI / 180.0);
        double      x0, z0, x1, z1;
        p.to_engine(a[0], a[1], x0, z0);
        p.to_engine(b[0], b[1], x1, z1);
        const double d = std::hypot(x1 - x0, z1 - z0);
        if (ref == 0.0) ref = d;
        CHECK(d == Approx(ref).epsilon(1e-9));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Compaction heuristic (spec §4.2)
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("compaction: landmark windows are kept at real scale", "[corridor][compaction]") {
    const double              N = 900.0, W = 250.0;
    const std::vector<double> marks{5000.0, 20000.0};
    CHECK(landmark_weight(5000.0, marks, N, W) == Approx(1.0));
    CHECK(landmark_weight(5000.0 + N, marks, N, W) == Approx(1.0));
    CHECK(landmark_weight(5000.0 + N + W, marks, N, W) == Approx(0.0));
    // Monotonic decay across the blend, and continuous at both ends.
    double prev = 1.0;
    for (double d = N; d <= N + W; d += 10.0) {
        const double w = landmark_weight(5000.0 + d, marks, N, W);
        CHECK(w <= prev + 1e-12);
        prev = w;
    }
}

TEST_CASE("compaction: solved c hits the target length", "[corridor][compaction]") {
    // The whole point of solving rather than authoring c: the corridor lands on
    // the target instead of being hand-walked into the 16-20 mi band.
    const double              step = 10.0;
    const int                 n    = 5000;  // 50 km of route
    std::vector<double>       w(n), radius(n, 1e18);
    const std::vector<double> marks{8000.0, 25000.0, 41000.0};
    for (int i = 0; i < n; ++i) w[i] = landmark_weight(i * step, marks, 900.0, 250.0);

    for (double targetMi : {16.0, 18.0, 20.0}) {
        const double target = targetMi * kMile;
        const double c      = solve_c(w, radius, step, target, 500.0);
        CHECK(c > 0.0);
        CHECK(c < 1.0);
        CHECK(total_length(w, radius, step, c, 500.0) == Approx(target).epsilon(1e-6));
    }
}

TEST_CASE("compaction: total length is monotonic in c", "[corridor][compaction]") {
    // What makes the bisection in solve_compaction valid once the curvature
    // floor turns the closed form into a piecewise-linear one.
    const double        step = 10.0;
    const int           n    = 2000;
    std::vector<double> w(n), radius(n);
    for (int i = 0; i < n; ++i) {
        w[i]      = landmark_weight(i * step, {6000.0}, 900.0, 250.0);
        radius[i] = 400.0 + 3000.0 * std::abs(std::sin(i * 0.01));  // a mix of tight and straight
    }
    double prev = -1.0;
    for (double c = 0.0; c <= 1.0; c += 0.02) {
        const double L = total_length(w, radius, step, c, 500.0);
        CHECK(L >= prev - 1e-9);
        prev = L;
    }
}

TEST_CASE("compaction: the curvature floor never tightens a real curve past the floor", "[corridor][compaction]") {
    // The bug stage 3a found: compaction preserves heading and scales arc
    // length, so R' = R*f — compressing a curve makes it TIGHTER by 1/f. At
    // c = 0.455 with no floor, this corridor's minimum radius fell from a real
    // 99 m to 46 m and sub-500 m mileage went 1.05 km -> 6.05 km, i.e. it
    // invented corners. The floor bounds exactly that.
    constexpr double floorR = 500.0;
    for (double c : {0.05, 0.1854, 0.5, 0.9}) {
        for (double R : {80.0, 99.0, 250.0, 499.0, 500.0, 501.0, 1200.0, 5000.0, 1e18}) {
            const double f  = scale_at(0.0, R, c, floorR);
            const double Rp = R * f;
            // Either the compacted radius clears the floor, or the REAL radius
            // was already below it and was left untouched (f == 1).
            CHECK((Rp >= floorR - 1e-6 || f == Approx(1.0)));
            CHECK(f <= 1.0 + 1e-12);
            CHECK(f >= c - 1e-12);
        }
    }
}

TEST_CASE("compaction: a curve tighter than the floor is left at real scale", "[corridor][compaction]") {
    // The real 99 m tunnel-portal ramp must survive verbatim — it is real, and
    // no amount of compaction should be allowed to make it worse.
    CHECK(scale_at(0.0, 99.0, 0.1854, 500.0) == Approx(1.0));
    CHECK(scale_at(0.0, 99.0, 0.1854, 500.0) * 99.0 == Approx(99.0));
}

TEST_CASE("compaction: straight road takes the full compression", "[corridor][compaction]") {
    // Where there is no landmark and no curvature, f == c: straightaways are
    // where the length budget is spent, which is the heuristic's whole intent.
    CHECK(scale_at(0.0, 1e18, 0.1854, 500.0) == Approx(0.1854));
    CHECK(scale_at(1.0, 1e18, 0.1854, 500.0) == Approx(1.0));  // inside a landmark window
}

// ══════════════════════════════════════════════════════════════════════
// Alignment queries
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("alignment: loads a baked asset and reports its header", "[corridor][alignment]") {
    const std::string path = write_synthetic("test_corridor_a.bin", 101, 10000.0f, 3.0f, 4.0f);
    CorridorAlignment al;
    al.load(path);
    REQUIRE(al.valid());
    CHECK(al.header().stationCount == 101u);
    CHECK(al.total_length() == Approx(1000000.0));
    CHECK(al.landmarks().size() == 1u);
    CHECK(std::string(al.landmarks()[0].ref) == "38");
}

TEST_CASE("alignment: a bad magic or version is a hard error, not a silent fallback", "[corridor][alignment]") {
    const std::string path = write_synthetic("test_corridor_bad.bin", 10, 10000.0f, 3.0f, 3.0f);
    // Corrupt the magic in place.
    FILE* f = std::fopen(path.c_str(), "r+b");
    REQUIRE(f != nullptr);
    uint32_t bad = 0xDEADBEEF;
    std::fwrite(&bad, sizeof(bad), 1, f);
    std::fclose(f);

    CorridorAlignment al;
    CHECK_THROWS(al.load(path));
    CHECK_THROWS(al.load("./definitely_not_a_corridor.bin"));
}

TEST_CASE("alignment: sample interpolates position, tangent and lane taper", "[corridor][alignment]") {
    const std::string path = write_synthetic("test_corridor_b.bin", 101, 10000.0f, 3.0f, 4.0f);
    CorridorAlignment al;
    al.load(path);

    auto a = al.sample(0.0);
    CHECK(a.z == Approx(0.0));
    CHECK(a.lanes == Approx(3.0f));
    CHECK(a.tz == Approx(-1.0f));

    auto mid = al.sample(500000.0);
    CHECK(mid.z == Approx(-500000.0));
    CHECK(mid.lanes == Approx(3.5f));

    // Halfway between two stations.
    auto q = al.sample(15000.0);
    CHECK(q.z == Approx(-15000.0));

    // Clamped, never extrapolated, at both ends.
    CHECK(al.sample(-1e6).z == Approx(0.0));
    CHECK(al.sample(1e9).z == Approx(-1000000.0));
    CHECK(al.sample(1e9).lanes == Approx(4.0f));

    // Tangents stay unit length after interpolation.
    for (double s = 0.0; s <= 1000000.0; s += 7333.0) {
        auto sm = al.sample(s);
        CHECK(std::hypot(sm.tx, sm.tz) == Approx(1.0f).epsilon(1e-5));
    }
}

TEST_CASE("alignment: project recovers station and signed lateral", "[corridor][alignment]") {
    const std::string path = write_synthetic("test_corridor_c.bin", 101, 10000.0f, 4.0f, 4.0f);
    CorridorAlignment al;
    al.load(path);

    // Travel is -Z, so right-of-travel is +X. A point +5000 WU in X off the
    // centreline at z = -300000 must read as lateral +5000 (eastbound side).
    size_t hint = 0;
    double s    = 0.0;
    float  lat  = 0.0f;
    al.project_global(5000.0, -300000.0, hint, s, lat);
    CHECK(s == Approx(300000.0).margin(1.0));
    CHECK(lat == Approx(5000.0f).margin(1.0f));

    // Mirror: -X is the westbound side.
    al.project_global(-5000.0, -300000.0, hint, s, lat);
    CHECK(s == Approx(300000.0).margin(1.0));
    CHECK(lat == Approx(-5000.0f).margin(1.0f));

    // The incremental (hinted) path must agree with the global one — this is
    // what the per-frame car tracking relies on.
    size_t h2 = al.index_at(295000.0);
    double s2 = 0.0;
    float  l2 = 0.0f;
    al.project(5000.0, -300000.0, h2, s2, l2);
    CHECK(s2 == Approx(300000.0).margin(1.0));
    CHECK(l2 == Approx(5000.0f).margin(1.0f));
}

TEST_CASE("alignment: cross-section offsets grow with lane count", "[corridor][alignment]") {
    const std::string path = write_synthetic("test_corridor_d.bin", 11, 10000.0f, 3.0f, 3.0f);
    CorridorAlignment al;
    al.load(path);
    const CorridorHeader& h = al.header();

    // paint_outer = median + inner shoulder + lanes*laneWidth
    CHECK(al.paint_inner(3.0f) == Approx(h.medianHalfWidth + h.innerShoulder));
    CHECK(al.paint_outer(3.0f) == Approx(h.medianHalfWidth + h.innerShoulder + 3.0f * h.laneWidth));
    CHECK(al.paint_outer(4.0f) - al.paint_outer(3.0f) == Approx(h.laneWidth));
    // The travelled way always sits inside the outer edge (shoulder beyond it).
    CHECK(al.outer_edge(3.0f) > al.paint_outer(3.0f));
    CHECK(al.inner_edge(3.0f) < al.paint_inner(3.0f));
}

TEST_CASE("alignment: turn-around pads exist only at the two ends", "[corridor][alignment]") {
    const std::string path = write_synthetic("test_corridor_e.bin", 101, 10000.0f, 3.0f, 3.0f);
    CorridorAlignment al;
    al.load(path);
    const double pad = al.header().turnaroundLength;
    CHECK(al.in_turnaround(0.0));
    CHECK(al.in_turnaround(pad * 0.5));
    CHECK_FALSE(al.in_turnaround(pad * 2.0));
    CHECK_FALSE(al.in_turnaround(al.total_length() * 0.5));
    CHECK(al.in_turnaround(al.total_length()));
}

// ══════════════════════════════════════════════════════════════════════
// Drivable bounds — what replaced the treadmill's world-X clamp
// ══════════════════════════════════════════════════════════════════════

TEST_CASE("driver: bounds confine the car to its own carriageway", "[corridor][driver]") {
    const std::string path = write_synthetic("test_corridor_f.bin", 401, 10000.0f, 3.0f, 3.0f);
    CorridorAlignment al;
    al.load(path);
    CorridorDriver d;
    d.set_alignment(&al);

    const double s = 2000000.0;  // well clear of both turn-around pads
    REQUIRE_FALSE(al.in_turnaround(s));

    float lo = 0.0f, hi = 0.0f;
    d.carriageway_bounds(s, +1, lo, hi);  // committed eastbound
    CHECK(lo > 0.0f);                     // never crosses the median
    CHECK(hi > lo);
    CHECK(hi < al.outer_edge(3.0f));   // stays inside the shoulder
    CHECK(hi > al.paint_outer(3.0f));  // but the shoulder IS run-off room

    float lo2 = 0.0f, hi2 = 0.0f;
    d.carriageway_bounds(s, -1, lo2, hi2);  // westbound mirrors it
    CHECK(hi2 < 0.0f);
    CHECK(lo2 == Approx(-hi));
    CHECK(hi2 == Approx(-lo));

    RoadGeometry::clear_flat_surface();
}

TEST_CASE("driver: bounds open across the median inside a turn-around pad", "[corridor][driver]") {
    const std::string path = write_synthetic("test_corridor_g.bin", 401, 10000.0f, 3.0f, 3.0f);
    CorridorAlignment al;
    al.load(path);
    CorridorDriver d;
    d.set_alignment(&al);

    float lo = 0.0f, hi = 0.0f;
    d.carriageway_bounds(0.0, +1, lo, hi);
    CHECK(lo < 0.0f);  // the U-turn needs the other side reachable
    CHECK(hi > 0.0f);
    CHECK(lo == Approx(-hi));

    RoadGeometry::clear_flat_surface();
}

TEST_CASE("driver: attaching an alignment redirects the surface-height query", "[corridor][driver]") {
    // A curving alignment makes world X meaningless as a lateral offset, so the
    // crown formula cannot be used; the corridor is flat instead, and geometry
    // and physics must read the SAME height.
    const std::string path = write_synthetic("test_corridor_h.bin", 11, 10000.0f, 3.0f, 3.0f);
    CorridorAlignment al;
    al.load(path);

    RoadGeometry::clear_flat_surface();
    const float crowned = RoadGeometry::road_surface_y(6501.0f);

    CorridorDriver d;
    d.set_alignment(&al);
    CHECK(RoadGeometry::flat_surface());
    // Same height everywhere, and it is the asset's roadY.
    CHECK(RoadGeometry::road_surface_y(6501.0f) == Approx(al.road_y()));
    CHECK(RoadGeometry::road_surface_y(-250000.0f) == Approx(al.road_y()));
    CHECK(RoadGeometry::road_surface_y(0.0f) == Approx(al.road_y()));

    // ...and the legacy crown path is restored exactly when it is turned off,
    // so nothing on the retired authored road changed.
    RoadGeometry::clear_flat_surface();
    CHECK_FALSE(RoadGeometry::flat_surface());
    CHECK(RoadGeometry::road_surface_y(6501.0f) == Approx(crowned));
}

// ══════════════════════════════════════════════════════════════════════
// Float32 safety — the spec §7 risk this stage had to confirm
// ══════════════════════════════════════════════════════════════════════

// Exact float32 ULP at v (one step of the bit pattern).
static double f32_ulp(double v) {
    const float f = static_cast<float>(v);
    return static_cast<double>(std::nextafter(f, std::numeric_limits<float>::infinity())) - static_cast<double>(f);
}

TEST_CASE("precision: the corridor's real scale is 2.9e7 WU, not 3e10", "[corridor][precision]") {
    // Correcting the record. The phase doc's §7 risk note put an 18-20 mi
    // corridor at ~3e10 WU "far beyond float32's exact-integer range". That
    // applied WORLD_SCALE twice: one mile is 1609.344 m x 1000 WU/m =
    // 1.609e6 WU, so 18 mi is 2.897e7 WU — a thousand times smaller.
    const double mileWU = kMile * kWorldScale;
    CHECK(mileWU == Approx(1609344.0));

    const double farEnd = 18.0 * mileWU;
    CHECK(farEnd == Approx(2.8968192e7));
    CHECK(farEnd < 1.0e8);  // NOT 3e10

    // At that magnitude float32's ULP is 2 WU = 2 mm, so absolute coordinates
    // are in fact usable — the 2048 WU (2 m) quantisation the note feared only
    // appears at the 3e10 it assumed.
    CHECK(f32_ulp(farEnd) == Approx(2.0));
    CHECK(f32_ulp(3.0e10) == Approx(2048.0));
}

TEST_CASE("precision: segment-local coordinates and the rebase are still worth it", "[corridor][precision]") {
    // 2 mm is survivable but marginal; the segment scheme costs nothing and
    // buys two orders of magnitude.
    CHECK(f32_ulp(6.0e5) < 0.07);  // segment-local vertex (~600 m from origin)
    CHECK(f32_ulp(1.2e6) < 0.13);  // car/camera under the rebase threshold
    CHECK(f32_ulp(2.4e6) < 0.26);  // a segment offset out at the far plane

    // The round trip the mesh generator + driver actually perform:
    //   vertex = float(worldPos - segmentOrigin)   [small, precise]
    //   offset = float(segmentOrigin + shift)      [computed in double]
    // must reconstruct the world position to well under a world unit even at
    // the corridor's east end.
    const double origin = -2.65e7;
    double       worst  = 0.0;
    for (double d = -600000.0; d <= 600000.0; d += 7333.0) {
        const double world  = origin + d;
        const float  vLocal = static_cast<float>(world - origin);
        const double back   = static_cast<double>(vLocal) + origin;
        worst               = std::max(worst, std::abs(back - world));
    }
    CHECK(worst < 0.1);

    // For contrast: the same sweep stored as ABSOLUTE float32 loses about a
    // world unit — the 2 WU ULP, halved by round-to-nearest. (Any individual
    // sample can land exactly on a representable value, so this has to be
    // measured over the sweep, not at one point.)
    double worstAbs = 0.0;
    for (double d = -600000.0; d <= 600000.0; d += 7333.0) {
        const double world = origin + d;
        worstAbs           = std::max(worstAbs, std::abs(static_cast<double>(static_cast<float>(world)) - world));
    }
    CHECK(worstAbs >= 0.5);
    CHECK(worstAbs > 10.0 * worst);
    CHECK(f32_ulp(origin) == Approx(2.0));
}

TEST_CASE("driver: a committed westbound car cannot be handed eastbound bounds", "[corridor][driver]") {
    // The bug this guards: bounds derived from the INSTANTANEOUS lateral sign
    // let one long frame (dt capped at 1/15 s, top speed 92,000 WU/s => 6.1 m
    // of lateral travel) cross the ~3.8 m median and then be told "you are
    // westbound now, carry on". The side is committed state, so the same
    // position on the wrong side of the median still gets its own side's band.
    const std::string path = write_synthetic("test_corridor_i.bin", 401, 10000.0f, 3.0f, 3.0f);
    CorridorAlignment al;
    al.load(path);
    CorridorDriver d;
    d.set_alignment(&al);

    const double s = 2000000.0;
    REQUIRE_FALSE(al.in_turnaround(s));

    float ebLo = 0.0f, ebHi = 0.0f, wbLo = 0.0f, wbHi = 0.0f;
    d.carriageway_bounds(s, +1, ebLo, ebHi);
    d.carriageway_bounds(s, -1, wbLo, wbHi);

    // The two bands are disjoint and the median sits between them, so a clamp
    // can never place a car on the far side of the barrier.
    CHECK(ebLo > 0.0f);
    CHECK(wbHi < 0.0f);
    CHECK(wbHi < ebLo);
    // A westbound-committed car sitting at an eastbound lateral is pulled BACK
    // across, not accepted where it is.
    const float strayed = +5000.0f;
    CHECK(std::clamp(strayed, wbLo, wbHi) == Approx(wbHi));
    CHECK(std::clamp(strayed, wbLo, wbHi) < 0.0f);

    RoadGeometry::clear_flat_surface();
}
