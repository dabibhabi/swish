#include "LightingLab.h"

#include "scene/RoadScene/MeshBuilder.h"
#include "scene/RoadScene/MeshShapes.h"
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>

namespace swish {

namespace {

using util::units::m;
namespace ul = util::light;

const Vec3 kUp(0.0f, 1.0f, 0.0f);

// ── Layout (metres). Lanes run along -Z. ─────────────────────────────
constexpr float kLaneStartZ = 50.0f;    // dashes begin just behind the stations
constexpr float kLaneEndZ   = -900.0f;  // and run past the last feature
constexpr float kStationZ   = -100.0f;  // teleports land 100 m before the first feature

constexpr float kTunnelUnlitX = -60.0f;
constexpr float kTunnelLitX   = -100.0f;
constexpr float kTunnelZ0     = -200.0f;  // mouth
constexpr float kTunnelLen    = 400.0f;
constexpr float kTunnelHalfW  = 6.0f;  // inner half-width
constexpr float kTunnelH      = 6.0f;  // inner height
constexpr float kTunnelWallT  = 0.6f;
constexpr float kTunnelRoofT  = 0.8f;
constexpr float kTunnelFade   = 20.0f;  // sky fades out over the first 20 m
// 12,000 lm every 12.5 m gives about 19 lux between luminaires and 33 under them (5.7 m up): an even tunnel.
// At 25 m the road between two lamps got only about 2 lux. A lab choice, not a measured fixture.
constexpr float kTunnelLampGap = 12.5f;
constexpr float kTunnelLampLm  = 12000.0f;

constexpr float                kOverpassX      = 60.0f;
constexpr float                kOverpassHalfL  = 20.0f;  // the deck spans 40 m across the lane
constexpr float                kOverpassClearH = 5.0f;   // about 16.5 ft
constexpr float                kOverpassDeckT  = 1.0f;
constexpr float                kOverpassAbutT  = 1.5f;
constexpr float                kOverpassFade   = 12.0f;
constexpr float                kOverpassGap    = 150.0f;
constexpr std::array<float, 4> kOverpassWidths{10.0f, 20.0f, 40.0f, 80.0f};

constexpr float kLampLaneX   = 140.0f;
constexpr float kLampOffsetX = 6.0f;  // poles stand 6 m right of the lane
constexpr float kLampZ0      = -200.0f;
constexpr float kLampLen     = 600.0f;
constexpr float kLampGap     = 40.0f;
constexpr float kLampH       = 9.0f;
constexpr float kLampLm      = 16000.0f;  // 150 W high-pressure sodium (typical), as on the road
constexpr float kLampRadius  = 50.0f;

// Showcase places (centres, metres).
constexpr float kStudioX = -200.0f, kStudioZ = 200.0f;
constexpr float kGarageX = -200.0f, kGarageZ = 300.0f;
constexpr float kCanopyX = 260.0f, kCanopyZ = 200.0f;
constexpr float kStreetX = 260.0f, kStreetZ = 330.0f;

constexpr float kLotX = 200.0f;
constexpr float kLotZ = 100.0f;  // the cards stand here and face +Z, toward the car at z = 120

const Vec4      kAsphaltTint(0.26f, 0.28f, 0.31f, 1.0f);
const Vec4      kConcrete(0.62f, 0.62f, 0.60f, 1.0f);
const Vec4      kPaint(0.85f, 0.85f, 0.82f, 1.0f);
const Vec3      kSodium(1.0f, 0.9f, 0.7f);
constexpr float kLampHeadNits = 20000.0f;  // a street-lamp head's luminance, cd/m² (16,000 lm from about 0.25 m²)

// Shapes in metres (the layout's unit); mesh_shapes works in WU.
void box(MeshBuilder& b, const Vec3& lo, const Vec3& hi, const Vec4& color, MaterialId mat) {
    mesh_shapes::box(b, lo * util::units::kWuPerMetre, hi * util::units::kWuPerMetre, color, mat);
}
void ball(MeshBuilder& b, const Vec3& centreM, float radiusM, const Vec4& color, MaterialId mat) {
    mesh_shapes::ball(b, centreM * util::units::kWuPerMetre, m(radiusM), color, mat);
}
// A lamp head: a box whose six faces glow (lighting L5.3). The renderer scales it by the frame's pre-exposure.
void glowing_box(LightingLabWorld& w, MeshBuilder& b, const Vec3& lo, const Vec3& hi) {
    const size_t first = w.draws.size();
    box(b, lo, hi, kPaint, MAT_DEFAULT);
    for (size_t i = first; i < w.draws.size(); ++i) w.draws[i].emissiveNits = kLampHeadNits;
}

void lamp(std::vector<LightDesc>& lights, const Vec3& posM, float lumens, float radiusM) {
    lights.push_back({posM * util::units::kWuPerMetre, kSodium, ul::point_candela_from_lumens(lumens), m(radiusM)});
}

void lane_dashes(MeshBuilder& b, float xM) {
    b.addDashedLine(m(xM - 0.08f), m(xM + 0.08f), m(0.01f), m(kLaneEndZ), m(kLaneStartZ), m(3.0f), m(9.0f), kUp, kPaint,
                    MAT_DEFAULT);
}

void lane_dashes_short(MeshBuilder& b, float xM, float z0M, float z1M) {
    b.addDashedLine(m(xM - 0.08f), m(xM + 0.08f), m(0.02f), m(z0M), m(z1M), m(3.0f), m(6.0f), kUp, kPaint, MAT_DEFAULT);
}

void tunnel(LightingLabWorld& w, MeshBuilder& b, float xM, bool lit) {
    const float z0 = kTunnelZ0, z1 = kTunnelZ0 - kTunnelLen;
    const float in = kTunnelHalfW, out = kTunnelHalfW + kTunnelWallT, top = kTunnelH + kTunnelRoofT;
    box(b, {xM - out, 0.0f, z1}, {xM - in, kTunnelH, z0}, kConcrete, MAT_CONCRETE);  // left wall
    box(b, {xM + in, 0.0f, z1}, {xM + out, kTunnelH, z0}, kConcrete, MAT_CONCRETE);  // right wall
    box(b, {xM - out, kTunnelH, z1}, {xM + out, top, z0}, kConcrete, MAT_CONCRETE);  // roof slab
    w.roofs.push_back({Vec3(m(xM - in), 0.0f, m(z1)), Vec3(m(xM + in), m(kTunnelH), m(z0)), m(kTunnelFade), 2, 0.02f});
    if (!lit) return;
    for (float z = z0 - kTunnelLampGap * 0.5f; z > z1; z -= kTunnelLampGap) {
        glowing_box(w, b, {xM - 0.4f, kTunnelH - 0.15f, z - 0.6f}, {xM + 0.4f, kTunnelH, z + 0.6f});
        lamp(w.lights, {xM, kTunnelH - 0.3f, z}, kTunnelLampLm, 30.0f);
    }
}

void overpasses(LightingLabWorld& w, MeshBuilder& b) {
    const float x0 = kOverpassX - kOverpassHalfL, x1 = kOverpassX + kOverpassHalfL;
    float       z = -200.0f;  // near edge of the next deck
    for (const float width : kOverpassWidths) {
        const float zFar = z - width;
        box(b, {x0, kOverpassClearH, zFar}, {x1, kOverpassClearH + kOverpassDeckT, z}, kConcrete, MAT_CONCRETE);
        box(b, {x0, 0.0f, zFar}, {x0 + kOverpassAbutT, kOverpassClearH, z}, kConcrete, MAT_CONCRETE);
        box(b, {x1 - kOverpassAbutT, 0.0f, zFar}, {x1, kOverpassClearH, z}, kConcrete, MAT_CONCRETE);
        w.roofs.push_back({Vec3(m(x0 + kOverpassAbutT), 0.0f, m(zFar)),
                           Vec3(m(x1 - kOverpassAbutT), m(kOverpassClearH), m(z)), m(kOverpassFade), 2, 0.03f});
        z = zFar - kOverpassGap;
    }
}

void lamp_stretch(LightingLabWorld& w, MeshBuilder& b) {
    const float px = kLampLaneX + kLampOffsetX;
    for (float z = kLampZ0; z >= kLampZ0 - kLampLen; z -= kLampGap) {
        box(b, {px - 0.1f, 0.0f, z - 0.1f}, {px + 0.1f, kLampH, z + 0.1f}, kConcrete, MAT_METAL);  // pole
        box(b, {kLampLaneX + 2.0f, kLampH - 0.2f, z - 0.15f}, {px + 0.1f, kLampH, z + 0.15f}, kConcrete,
            MAT_METAL);  // arm
        glowing_box(w, b, {kLampLaneX + 1.6f, kLampH - 0.4f, z - 0.3f}, {kLampLaneX + 2.4f, kLampH - 0.2f, z + 0.3f});
        lamp(w.lights, {kLampLaneX + 2.0f, kLampH - 0.5f, z}, kLampLm, kLampRadius);
    }
}

void calibration_lot(LightingLabWorld& w, MeshBuilder& b) {
    // Reflectance cards, 2 m square, 1 m off the ground, facing the car (+Z). MAT_DEFAULT is a white texture
    // with roughness 1, so the tint IS the albedo: a card under E lux has luminance rho * E / pi.
    const std::array<float, 3> rho{0.90f, 0.18f, 0.03f};  // white, 18% grey, black
    for (size_t i = 0; i < rho.size(); ++i) {
        const float x = kLotX - 6.0f + 4.0f * static_cast<float>(i);
        box(b, {x - 1.0f, 1.0f, kLotZ - 0.05f}, {x + 1.0f, 3.0f, kLotZ + 0.05f}, Vec4(Vec3(rho[i]), 1.0f), MAT_DEFAULT);
        box(b, {x - 0.05f, 0.0f, kLotZ - 0.05f}, {x + 0.05f, 1.0f, kLotZ + 0.05f}, kConcrete, MAT_METAL);  // stand
        // The same three reflectances lying flat, so they read the light falling on the ground.
        b.addHorizontalQuad(m(x - 1.5f), m(x + 1.5f), m(0.012f), m(kLotZ + 6.0f), m(kLotZ + 9.0f), kUp,
                            Vec4(Vec3(rho[i]), 1.0f), MAT_DEFAULT);
    }
    ball(b, {kLotX + 7.0f, 1.0f, kLotZ + 2.0f}, 1.0f, Vec4(0.5f, 0.5f, 0.5f, 1.0f), MAT_DEFAULT);  // matte grey ball
    ball(b, {kLotX + 10.5f, 1.0f, kLotZ + 2.0f}, 1.0f, Vec4(0.9f, 0.9f, 0.9f, 1.0f), MAT_METAL);   // metal ball
    // One lamp on a 6 m pole, so the lot has a known point source: E = I / d^2 on the cards.
    box(b, {kLotX - 11.1f, 0.0f, kLotZ + 3.9f}, {kLotX - 10.9f, 6.0f, kLotZ + 4.1f}, kConcrete, MAT_METAL);
    lamp(w.lights, {kLotX - 11.0f, 6.0f, kLotZ + 4.0f}, kLampLm, kLampRadius);
}

// A rectangle area light plus the glowing panel you see (a thin slab just behind the emitting face). The light
// emits along cross(right, up). Sizes in metres, luminance in cd/m², colour from a temperature in kelvin.
void panel(LightingLabWorld& w, MeshBuilder& b, const Vec3& centreM, const Vec3& right, const Vec3& up, float halfW,
           float halfH, float nits, float kelvin, float rangeM) {
    const Vec3 colour = ul::kelvin_to_rgb(kelvin);
    const Vec3 n      = glm::normalize(glm::cross(right, up));
    w.rects.push_back({centreM * util::units::kWuPerMetre, right, up, m(halfW), m(halfH), colour, nits, m(rangeM)});
    const size_t first = w.draws.size();
    mesh_shapes::oriented_box(b, (centreM - n * 0.03f) * util::units::kWuPerMetre, right * m(halfW), up * m(halfH),
                              n * m(0.025f), Vec4(colour, 1.0f), MAT_DEFAULT);
    for (size_t i = first; i < w.draws.size(); ++i) w.draws[i].emissiveNits = nits;
}

// Studio: a closed, dark room. One large soft panel overhead and a vertical strip on each side wall: the
// classic three-light set-up for reading a car's surfaces. 5,600 K (daylight-balanced studio light).
void studio(LightingLabWorld& w, MeshBuilder& b) {
    const float cx = kStudioX, cz = kStudioZ, hx = 9.0f, hz = 13.0f, hgt = 6.5f, t = 0.3f;
    const Vec4  dark(0.06f, 0.06f, 0.065f, 1.0f), floorTint(0.16f, 0.16f, 0.17f, 1.0f);
    box(b, {cx - hx - t, 0.0f, cz - hz - t}, {cx - hx, hgt, cz + hz + t}, dark, MAT_DEFAULT);
    box(b, {cx + hx, 0.0f, cz - hz - t}, {cx + hx + t, hgt, cz + hz + t}, dark, MAT_DEFAULT);
    box(b, {cx - hx, 0.0f, cz - hz - t}, {cx + hx, hgt, cz - hz}, dark, MAT_DEFAULT);
    box(b, {cx - hx, 0.0f, cz + hz}, {cx + hx, hgt, cz + hz + t}, dark, MAT_DEFAULT);
    box(b, {cx - hx - t, hgt, cz - hz - t}, {cx + hx + t, hgt + t, cz + hz + t}, dark, MAT_DEFAULT);
    b.addHorizontalQuad(m(cx - hx), m(cx + hx), m(0.015f), m(cz - hz), m(cz + hz), kUp, floorTint, MAT_DEFAULT);
    w.roofs.push_back({Vec3(m(cx - hx), 0.0f, m(cz - hz)), Vec3(m(cx + hx), m(hgt), m(cz + hz)), m(1.0f), -1, 0.0f});
    panel(w, b, {cx, hgt - 0.6f, cz}, Vec3(1, 0, 0), Vec3(0, 0, 1), 3.0f, 5.0f, 3000.0f, 5600.0f,
          30.0f);  // overhead, down
    panel(w, b, {cx - hx + 0.4f, 1.6f, cz}, Vec3(0, 0, -1), Vec3(0, 1, 0), 4.0f, 0.9f, 2000.0f, 5600.0f,
          25.0f);  // left, facing +X
    panel(w, b, {cx + hx - 0.4f, 1.6f, cz}, Vec3(0, 0, 1), Vec3(0, 1, 0), 4.0f, 0.9f, 2000.0f, 5600.0f,
          25.0f);  // right, facing -X
}

// Parking garage: a low concrete deck on pillars, lit by rows of tube lights (4,000 K, about 9,000 cd/m²).
void garage(LightingLabWorld& w, MeshBuilder& b) {
    const float cx = kGarageX, cz = kGarageZ, hx = 15.0f, hz = 20.0f, hgt = 2.8f, t = 0.3f;
    box(b, {cx - hx - t, 0.0f, cz - hz - t}, {cx - hx, hgt, cz + hz + t}, kConcrete, MAT_CONCRETE);
    box(b, {cx + hx, 0.0f, cz - hz - t}, {cx + hx + t, hgt, cz + hz + t}, kConcrete, MAT_CONCRETE);
    box(b, {cx - hx, 0.0f, cz + hz}, {cx + hx, hgt, cz + hz + t}, kConcrete, MAT_CONCRETE);
    box(b, {cx - hx - t, hgt, cz - hz - t}, {cx + hx + t, hgt + 0.4f, cz + hz + t}, kConcrete,
        MAT_CONCRETE);  // the deck above
    for (float px = cx - 10.0f; px <= cx + 10.0f; px += 10.0f)
        for (float pz = cz - 15.0f; pz <= cz + 15.0f; pz += 7.5f)
            box(b, {px - 0.25f, 0.0f, pz - 0.25f}, {px + 0.25f, hgt, pz + 0.25f}, kConcrete, MAT_CONCRETE);
    // Open at the -Z end only (the way the car faces): daylight reaches a little way in.
    w.roofs.push_back({Vec3(m(cx - hx), 0.0f, m(cz - hz)), Vec3(m(cx + hx), m(hgt), m(cz + hz)), m(8.0f), 4, 0.01f});
    for (float tz = cz - 16.0f; tz <= cz + 16.0f; tz += 6.4f)
        for (const float tx : {cx - 5.0f, cx + 5.0f})
            panel(w, b, {tx, hgt - 0.08f, tz}, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.6f, 0.075f, 9000.0f, 4000.0f, 14.0f);
    lane_dashes_short(b, cx - 5.0f, cz - hz, cz + hz);
    lane_dashes_short(b, cx + 5.0f, cz - hz, cz + hz);
}

// Gas station canopy: a slab on four columns with LED panels underneath (6,500 K), over two pump islands.
void gas_canopy(LightingLabWorld& w, MeshBuilder& b) {
    const float cx = kCanopyX, cz = kCanopyZ, hx = 8.0f, hz = 6.0f, hgt = 5.0f;
    const Vec4  white(0.85f, 0.85f, 0.86f, 1.0f), red(0.6f, 0.06f, 0.05f, 1.0f);
    box(b, {cx - hx, hgt, cz - hz}, {cx + hx, hgt + 0.6f, cz + hz}, white, MAT_DEFAULT);
    box(b, {cx - hx, hgt + 0.05f, cz - hz - 0.02f}, {cx + hx, hgt + 0.55f, cz - hz}, red,
        MAT_DEFAULT);  // fascia stripe
    for (const float sx : {-1.0f, 1.0f})
        for (const float sz : {-1.0f, 1.0f})
            box(b, {cx + sx * 6.5f - 0.2f, 0.0f, cz + sz * 4.0f - 0.2f},
                {cx + sx * 6.5f + 0.2f, hgt, cz + sz * 4.0f + 0.2f}, white, MAT_METAL);
    for (const float sx : {-1.0f, 1.0f}) {
        box(b, {cx + sx * 3.2f - 0.5f, 0.0f, cz - 2.5f}, {cx + sx * 3.2f + 0.5f, 0.18f, cz + 2.5f}, kConcrete,
            MAT_CONCRETE);
        box(b, {cx + sx * 3.2f - 0.3f, 0.18f, cz - 0.5f}, {cx + sx * 3.2f + 0.3f, 1.9f, cz + 0.5f}, white, MAT_METAL);
    }
    w.roofs.push_back({Vec3(m(cx - hx), 0.0f, m(cz - hz)), Vec3(m(cx + hx), m(hgt), m(cz + hz)), m(4.0f), 3, 0.2f});
    for (const float px : {cx - 5.0f, cx - 1.7f, cx + 1.7f, cx + 5.0f})
        for (const float pz : {cz - 3.0f, cz + 3.0f})
            panel(w, b, {px, hgt - 0.03f, pz}, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.45f, 0.45f, 15000.0f, 6500.0f, 22.0f);
}

// Night street: two rows of buildings with lit shop windows (3,000 K) and sodium lamps. Press R for rain.
void night_street(LightingLabWorld& w, MeshBuilder& b) {
    const float cx = kStreetX, cz = kStreetZ, len = 60.0f, roadHalf = 5.0f, walk = 2.5f, bh = 9.0f;
    const Vec4  brick(0.22f, 0.13f, 0.10f, 1.0f), stone(0.35f, 0.34f, 0.33f, 1.0f);
    for (const float side : {-1.0f, 1.0f}) {
        const float inner = cx + side * (roadHalf + walk), outer = cx + side * (roadHalf + walk + 8.0f);
        box(b, {std::min(inner, outer), 0.0f, cz - len * 0.5f}, {std::max(inner, outer), bh, cz + len * 0.5f},
            side < 0 ? brick : stone, MAT_CONCRETE);
        const float kerb0 = cx + side * roadHalf, kerb1 = cx + side * (roadHalf + walk);
        box(b, {std::min(kerb0, kerb1), 0.0f, cz - len * 0.5f}, {std::max(kerb0, kerb1), 0.15f, cz + len * 0.5f},
            kConcrete, MAT_CONCRETE);
        // Shop windows on the wall, facing the street: cross(right, up) must point at the road.
        const Vec3 right = side < 0 ? Vec3(0, 0, -1) : Vec3(0, 0, 1);
        for (float wz = cz - 20.0f; wz <= cz + 20.0f; wz += 20.0f)
            panel(w, b, {inner - side * 0.02f, 1.7f, wz}, right, Vec3(0, 1, 0), 2.2f, 1.1f, 600.0f, 3000.0f, 24.0f);
    }
    for (float lz = cz - 20.0f; lz <= cz + 20.0f; lz += 20.0f) {
        const float px = cx + roadHalf + 0.6f;
        box(b, {px - 0.08f, 0.0f, lz - 0.08f}, {px + 0.08f, 7.0f, lz + 0.08f}, kConcrete, MAT_METAL);
        glowing_box(w, b, {px - 2.0f, 6.8f, lz - 0.25f}, {px - 1.2f, 7.0f, lz + 0.25f});
        lamp(w.lights, {px - 1.6f, 6.7f, lz}, 12000.0f, 35.0f);
    }
    lane_dashes_short(b, cx, cz - len * 0.5f, cz + len * 0.5f);
}

void perimeter(MeshBuilder& b) {
    const float h = kLabHalfSizeM, t = 0.6f, top = 1.2f;
    box(b, {-h - t, 0.0f, -h - t}, {h + t, top, -h}, kConcrete, MAT_CONCRETE);
    box(b, {-h - t, 0.0f, h}, {h + t, top, h + t}, kConcrete, MAT_CONCRETE);
    box(b, {-h - t, 0.0f, -h}, {-h, top, h}, kConcrete, MAT_CONCRETE);
    box(b, {h, 0.0f, -h}, {h + t, top, h}, kConcrete, MAT_CONCRETE);
}

}  // namespace

const std::array<LabStation, static_cast<size_t>(LabStationId::Count)>& lab_stations() {
    static const std::array<LabStation, static_cast<size_t>(LabStationId::Count)> kStations{{
        {"Start (open pavement)", Vec3(0.0f, 0.0f, 0.0f), 90.0f},
        {"Unlit tunnel", Vec3(m(kTunnelUnlitX), 0.0f, m(kStationZ)), 90.0f},
        {"Lit tunnel", Vec3(m(kTunnelLitX), 0.0f, m(kStationZ)), 90.0f},
        {"Overpass series", Vec3(m(kOverpassX), 0.0f, m(kStationZ)), 90.0f},
        {"Lamp stretch", Vec3(m(kLampLaneX), 0.0f, m(kStationZ)), 90.0f},
        {"Calibration lot", Vec3(m(kLotX), 0.0f, m(kLotZ + 20.0f)), 90.0f},
        {"Studio", Vec3(m(kStudioX), 0.0f, m(kStudioZ)), 90.0f},
        {"Parking garage", Vec3(m(kGarageX - 5.0f), 0.0f, m(kGarageZ)), 90.0f},  // in the left aisle, between pillars
        {"Gas canopy (night)", Vec3(m(kCanopyX), 0.0f, m(kCanopyZ)), 90.0f, 5},
        {"Night street (R = rain)", Vec3(m(kStreetX), 0.0f, m(kStreetZ + 20.0f)), 90.0f, 5},
    }};
    return kStations;
}

LightingLabWorld build_lighting_lab() {
    LightingLabWorld w;
    MeshBuilder      b(w.mesh, w.draws);

    // The paved box: one quad a little larger than the wall, the lookdev pad's asphalt.
    const float g = m(kLabHalfSizeM + 5.0f);
    b.addHorizontalQuad(-g, g, 0.0f, -g, g, kUp, kAsphaltTint, MAT_ASPHALT, 20000.0f);
    perimeter(b);

    for (const float x : {0.0f, kTunnelUnlitX, kTunnelLitX, kOverpassX, kLampLaneX}) lane_dashes(b, x);
    tunnel(w, b, kTunnelUnlitX, false);
    tunnel(w, b, kTunnelLitX, true);
    overpasses(w, b);
    lamp_stretch(w, b);
    calibration_lot(w, b);
    studio(w, b);
    garage(w, b);
    gas_canopy(w, b);
    night_street(w, b);
    return w;
}

Vec3 lab_clamp_to_wall(const Vec3& p) {
    const float lim = m(kLabHalfSizeM - kLabWallMarginM);
    return Vec3(std::clamp(p.x, -lim, lim), p.y, std::clamp(p.z, -lim, lim));
}

LabProbe lab_probe(std::span<const LightDesc> lights, std::span<const ul::RoofVolume> roofs, const Vec3& p) {
    LabProbe out;
    // Sample 1 m above the ground: the volumes start at y = 0 and a point exactly on a face is ambiguous.
    out.skyVisibility = ul::sky_visibility(roofs, p + Vec3(0.0f, m(1.0f), 0.0f));
    for (const LightDesc& l : lights) {
        const Vec3  d    = l.position - p;
        const float dist = glm::length(d);
        if (out.nearestLampM < 0.0f || util::units::to_m(dist) < out.nearestLampM)
            out.nearestLampM = util::units::to_m(dist);
        if (dist >= l.radius) continue;
        ++out.lampsInRange;
        float win =
            std::clamp(1.0f - std::pow(dist / l.radius, 4.0f), 0.0f, 1.0f);  // the Karis window, as lighting.frag
        win *= win;
        const float cosTheta = std::max(d.y, 0.0f) / std::max(dist, 1.0f);  // ground normal is +Y
        out.lampLux += l.intensity * ul::inverse_square_m(dist) * win * cosTheta;
    }
    return out;
}

}  // namespace swish
