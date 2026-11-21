// scene/LightingLab: the debug-only lighting test world (tasks/todo.md "Lighting lab"). GPU-free.
#include "scene/LightingLab/LightingLab.h"
#include "scene/RoadScene/MeshShapes.h"
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace swish;
using Catch::Approx;
namespace ul = util::light;
namespace uu = util::units;

TEST_CASE("Lab: the world has geometry, lamps and roof volumes the shader can hold", "[lighting][lab]") {
    const LightingLabWorld w = build_lighting_lab();
    CHECK(w.mesh.getVertexCount() > 0);
    CHECK(w.mesh.getIndexCount() % 3 == 0);
    CHECK_FALSE(w.draws.empty());
    CHECK(w.roofs.size() == 9);  // two tunnels, four overpasses, the studio, the garage and the canopy
    CHECK(w.roofs.size() <= static_cast<size_t>(ul::kMaxRoofVolumes));
    CHECK(w.lights.size() == 32 + 16 + 1 + 3);  // lit tunnel, lamp stretch, calibration lot, night street

    const LightingLabWorld again = build_lighting_lab();  // deterministic
    CHECK(again.mesh.getVertexCount() == w.mesh.getVertexCount());
    CHECK(again.draws.size() == w.draws.size());
}

TEST_CASE("Lab: everything sits inside the 2 mile wall", "[lighting][lab]") {
    const LightingLabWorld w   = build_lighting_lab();
    const float            lim = uu::m(kLabHalfSizeM);
    CHECK(kLabHalfSizeM * 2.0f == Approx(3200.0f));  // 3.2 km, about 2 miles
    for (const LightDesc& l : w.lights) {
        CHECK(std::abs(l.position.x) < lim);
        CHECK(std::abs(l.position.z) < lim);
        CHECK(l.intensity > 0.0f);
        CHECK(l.radius > 0.0f);
    }
    for (const ul::RoofVolume& r : w.roofs) {
        CHECK(r.min.x < r.max.x);
        CHECK(r.min.y < r.max.y);
        CHECK(r.min.z < r.max.z);
        CHECK(std::abs(r.min.z) < lim);
        CHECK(r.fadeWu > 0.0f);
    }
    for (const LabStation& s : lab_stations()) {
        CHECK(lab_clamp_to_wall(s.position) == s.position);  // a teleport never lands in the wall
        CHECK(s.name != nullptr);
    }
}

TEST_CASE("Lab: the wall clamp holds x and z and leaves y alone", "[lighting][lab]") {
    const float lim = uu::m(kLabHalfSizeM - kLabWallMarginM);
    const Vec3  out = lab_clamp_to_wall(Vec3(uu::m(5000.0f), 123.0f, uu::m(-5000.0f)));
    CHECK(out.x == Approx(lim));
    CHECK(out.z == Approx(-lim));
    CHECK(out.y == Approx(123.0f));
    const Vec3 inside(uu::m(10.0f), 0.0f, uu::m(-20.0f));
    CHECK(lab_clamp_to_wall(inside) == inside);
}

TEST_CASE("Lab: road stations start under open sky; showcase places are under their roof", "[lighting][lab]") {
    const LightingLabWorld w = build_lighting_lab();
    // The road stations start in the open; the showcase places put the car under their roof.
    const auto& st = lab_stations();
    for (size_t i = 0; i <= static_cast<size_t>(LabStationId::CalibrationLot); ++i)
        CHECK(lab_probe(w.lights, w.roofs, st[i].position).skyVisibility == 1.0f);
    CHECK(lab_probe(w.lights, w.roofs, st[static_cast<size_t>(LabStationId::Studio)].position).skyVisibility == 0.0f);
    CHECK(lab_probe(w.lights, w.roofs, st[static_cast<size_t>(LabStationId::Garage)].position).skyVisibility < 0.05f);
    CHECK(lab_probe(w.lights, w.roofs, st[static_cast<size_t>(LabStationId::GasCanopy)].position).skyVisibility < 0.5f);
    CHECK(lab_probe(w.lights, w.roofs, st[static_cast<size_t>(LabStationId::NightStreet)].position).skyVisibility ==
          1.0f);
}

TEST_CASE("Lab: the probe reads darkness in the unlit tunnel and lamp light in the lit one", "[lighting][lab]") {
    const LightingLabWorld w  = build_lighting_lab();
    const auto&            st = lab_stations();
    const Vec3 unlit = st[static_cast<size_t>(LabStationId::TunnelUnlit)].position + Vec3(0, 0, uu::m(-300.0f));
    const Vec3 lit   = st[static_cast<size_t>(LabStationId::TunnelLit)].position + Vec3(0, 0, uu::m(-300.0f));

    const LabProbe a = lab_probe(w.lights, w.roofs, unlit);  // 200 m into the unlit tunnel
    CHECK(a.skyVisibility == Approx(0.02f));
    CHECK(a.lampLux < 0.5f);  // the lit tunnel is 40 m away behind a wall; only stray windowed light

    const LabProbe b = lab_probe(w.lights, w.roofs, lit);
    CHECK(b.skyVisibility == Approx(0.02f));
    CHECK(b.lampsInRange >= 2);
    CHECK(b.lampLux > 10.0f);  // 12,000 lm luminaires 5.7 m up, every 12.5 m: about 19 to 33 lux
    CHECK(b.lampLux < 60.0f);
}

TEST_CASE("Lab: a street lamp on the lamp stretch gives road-like lux under it", "[lighting][lab]") {
    const LightingLabWorld w     = build_lighting_lab();
    const Vec3             start = lab_stations()[static_cast<size_t>(LabStationId::LampStretch)].position;
    // The first lamp hangs 2 m right of the lane at z = -200 m; the station is at z = -100 m.
    const LabProbe under = lab_probe(w.lights, w.roofs, start + Vec3(uu::m(2.0f), 0.0f, uu::m(-100.0f)));
    CHECK(under.nearestLampM == Approx(8.5f).margin(0.1));  // the lamp head is 8.5 m up
    CHECK(under.lampLux > 10.0f);                           // real roads: about 10 to 30 lux
    CHECK(under.lampLux < 30.0f);
    const LabProbe far = lab_probe(w.lights, w.roofs, start);  // 100 m short of the first lamp
    CHECK(far.lampLux == Approx(0.0f).margin(1.0e-3));
}

TEST_CASE("Lab: lamp heads glow and nothing else does", "[lighting][lab][L5]") {
    const LightingLabWorld w       = build_lighting_lab();
    size_t                 glowing = 0;
    for (const DrawCall& d : w.draws) {
        CHECK(d.emissiveNits >= 0.0f);
        if (d.emissiveNits > 0.0f) ++glowing;
    }
    CHECK(glowing == (32 + 16 + 3 + w.rects.size()) * 6);  // six faces per lamp head and per area-light panel
}

TEST_CASE("Mesh shapes: every face of a box points outward", "[lighting][lab][mesh_shapes]") {
    MeshData              mesh;
    std::vector<DrawCall> draws;
    MeshBuilder           b(mesh, draws);
    const Vec3            lo(-1.0f, 0.0f, -2.0f), hi(3.0f, 5.0f, 4.0f), centre = 0.5f * (lo + hi);
    mesh_shapes::box(b, lo, hi, Vec4(1.0f), MAT_DEFAULT);
    REQUIRE(draws.size() == 6);
    REQUIRE(mesh.getVertexCount() == 24);
    REQUIRE(mesh.getIndexCount() == 36);
    const auto& v   = mesh.getVertices();
    const auto& idx = mesh.getIndices();
    for (size_t t = 0; t < idx.size(); t += 3) {
        const Vec3 a = v[idx[t]].position, bb = v[idx[t + 1]].position, c = v[idx[t + 2]].position;
        const Vec3 faceN = glm::normalize(glm::cross(bb - a, c - a));
        CHECK(glm::dot(faceN, v[idx[t]].normal) > 0.99f);             // winding agrees with the stored normal
        CHECK(glm::dot(faceN, (a + bb + c) / 3.0f - centre) > 0.0f);  // and the normal points away from the centre
    }
}

TEST_CASE("Showcase: area lights are well formed and face into their place", "[lighting][lab][showcase]") {
    const LightingLabWorld w  = build_lighting_lab();
    const auto&            st = lab_stations();
    CHECK(w.rects.size() == 3 + 12 + 8 + 6);  // studio, garage tubes, canopy panels, shop windows
    for (const RectLightDesc& r : w.rects) {
        CHECK(glm::length(r.right) == Approx(1.0f));
        CHECK(glm::length(r.up) == Approx(1.0f));
        CHECK(std::abs(glm::dot(r.right, r.up)) < 1.0e-4f);
        CHECK(r.halfWidth > 0.0f);
        CHECK(r.halfHeight > 0.0f);
        CHECK(r.nits > 0.0f);
        CHECK(r.range > 0.0f);
    }
    // The studio's three panels all emit toward the car (the light's direction is cross(right, up)).
    const Vec3 car    = st[static_cast<size_t>(LabStationId::Studio)].position + Vec3(0.0f, uu::m(0.7f), 0.0f);
    int        facing = 0;
    for (size_t i = 0; i < 3; ++i) {
        const RectLightDesc& r = w.rects[i];
        if (glm::dot(glm::cross(r.right, r.up), car - r.center) > 0.0f) ++facing;
    }
    CHECK(facing == 3);
    // Night places switch to Midnight on teleport; the others leave the time of day alone.
    CHECK(st[static_cast<size_t>(LabStationId::GasCanopy)].timeOfDay == 5);
    CHECK(st[static_cast<size_t>(LabStationId::NightStreet)].timeOfDay == 5);
    CHECK(st[static_cast<size_t>(LabStationId::Studio)].timeOfDay == -1);
}
