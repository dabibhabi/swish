# Code snippets

## Detached ImGui controls window

See [DebugUI::present](../src/debug/DebugUI.cpp) for the independent acquire/record/submit/present loop. It owns a second GLFW surface and swapchain; the main renderer presents the game view separately so controls do not cover the car while it is being driven.

## Lighting workbench launch

`make lookdev` sets `SWISH_LOOKDEV=1` and launches the debug build on the focused studio pad; details are in [lighting-workbench.md](../docs/lighting-workbench.md).

The look-dev road half-length, driving bound, camera far plane, and end margin share compile-time constants in [AppInit.cpp](../src/core/App/AppInit.cpp). The car bound is derived as `roadHalfLength - cameraFar - endMargin`, keeping the road end beyond the rendered horizon.

## Lighting scaffold: physical light units

[Photometry.h](../src/utils/light/Photometry.h) holds the CPU side of the unit maths (EV100 to exposure, lumens to candela, inverse-square in metres, colour temperature, emissive packing) as stubs to fill in. It exists so the same formulas can be unit-tested before they are typed into `lighting.frag`; the guide is [L01-physical-units.md](../docs/tutorials/lighting/L01-physical-units.md).

## Lighting scaffold: stable shadow maps

[ShadowFit.h](../src/utils/light/ShadowFit.h) stubs the four pure rules behind shadow stability: a light up vector that survives a sun at the zenith (the near-road blackout), texel snapping, per-cascade bias and hysteresis when choosing which lamps cast shadows. Guides: [L03-sun-shadows.md](../docs/tutorials/lighting/L03-sun-shadows.md) and [L07-local-shadows.md](../docs/tutorials/lighting/L07-local-shadows.md).

## Lighting scaffold: time-of-day presets

[TimeOfDay.h](../src/scene/TimeOfDay/TimeOfDay.h) stubs the preset struct, the sun direction from azimuth and elevation, and the reader for [config/time_of_day.toml](../config/time_of_day.toml). It follows the `read_float` plus fallback pattern of [CarConfig.cpp](../src/scene/CarConfig/CarConfig.cpp) so a bad config can never break startup.

## Lighting scaffold: corridor street lamps

[CorridorLamps.h](../src/scene/Corridor/CorridorLamps.h) stubs lamp placement along a centre-line polyline. It is the port target for the straight-road loop in [RoadScene_Furniture.cpp](../src/scene/RoadScene/RoadScene_Furniture.cpp) (`generate_street_lamps`), needed because the driven corridor curves and currently has no lamps.

## Live HDRI selection

[HdriCatalog.h](../src/renderer/IBLManager/HdriCatalog.h) is the shared option list for the Sky panel and [WeatherController](../src/renderer/WeatherController/WeatherController.cpp). Keeping labels, filenames, exposure and yaw in one catalog ensures a debug selection performs one matching environment rebake and can be restored in a saved debug look preset.

## Box-filtered line coverage (D1 pavement, 2026-11-21)

[shaders/common/pavement.glsl](../shaders/common/pavement.glsl) `paveBand`: anti-aliased thin lines (joints, sealant, rumble grooves) that fade to their mean tone when sub-pixel instead of shimmering. Reuse for any procedural stripe keyed to UVs.

```glsl
float paveBand(float d, float width, float fw) {
    float f = max(fw, 1e-3);
    float h = 0.5 * width;
    return clamp((min(d + 0.5 * f, h) - max(d - 0.5 * f, -h)) / f, 0.0, 1.0);
}
```

## Fold-free LIE warp: fixed cross axis instead of the local normal (N1, 2026-11-21)

[tools/queens_network.py](../tools/queens_network.py) `LieWarp.frame`. Why: placing junctions km off the LIE
with the local normal folded (det -103), because the nearest-point projection jumps past the LIE's centres of
curvature. Solving `P_z(s) = p_z` instead makes `s(p)` unique while the LIE heads east, and det dW/dp = f(s) > 0.

```python
def frame(self, p):
    i = min(max(bisect.bisect_left(self._negz, -p[1]) - 1, 0), len(self.xy) - 2)
    a, b = self.xy[i], self.xy[i + 1]
    u = (p[1] - a[1]) / (b[1] - a[1])
    return self.s[i] + u * (self.s[i + 1] - self.s[i]), p[0] - (a[0] + u * (b[0] - a[0]))
```

## N2a height-continuity handoff (2026-11-21)

[CorridorNetwork::step](../src/scene/Corridor/CorridorNetwork.cpp): the car stays on its piece while it holds it; else
the candidate (a road at a ramp end first, then other ramps) that holds it within `road::network::kHandoffDy` of last
frame's surface height; else clamp. Needed so an overpassing road or a ramp above or below never grabs the car.

```cpp
const float dy = std::abs(ramp_surface(o, ou, ol) - y);
if (ou < 0.0 || ou > o.length() || std::abs(ol) > o.halfWidth - inset || dy > bdy) continue;
```

## N3 one-way ramps in the handoff (2026-11-22)

[CorridorNetwork::step](../src/scene/Corridor/CorridorNetwork.cpp) and `enter_ramp`: a ramp only takes the car if it
runs the car's way (`heading_dot >= 0`), and at a fork the sibling with the nearer centreline takes it while both are
level. Needed because a two-way OSM link pair, squeezed into the LIE's compact median, overlapped at one height and
captured the car into a dead end.

```cpp
if (ou < 0.0 || ou > o.length() || std::abs(ol) > o.halfWidth - inset || dy > bdy ||
    heading_dot(o, ou, hx, hz) < 0.0f)
    continue;
```

## N4 one side per ramp crossing (2026-11-22)

[CorridorNetwork::clear_ramps](../src/scene/Corridor/CorridorNetwork.cpp): a run of ramp points over a road's pavement
goes wholly over or wholly under it, by the run's mean gap. Needed because the per-point choice flipped where a C/D ramp
ran near level with the GCP under the LIE (exit 22) and tore the ramp by 10 m (a 2.5 m step in one metre of driving).

```cpp
for (size_t a = 0, b = 0; a < gap.size(); a = b) {
    for (b = a + 1; b < gap.size() && (gap[a] < 1e29f) == (gap[b] < 1e29f);) ++b;
    if (gap[a] >= 1e29f) continue;
    float mean = 0.0f;
    for (size_t k = a; k < b; ++k) mean += gap[k] / static_cast<float>(b - a);
    for (size_t k = a; k < b; ++k) {  // over: raise to clear; under: drop to clear below
        const float n = mean >= -m(0.5f) ? std::max(0.0f, clear - gap[k]) : std::min(0.0f, -clear - gap[k]);
        if (std::abs(n) > std::abs(need[k])) need[k] = n;
    }
}
```

## N5: hand-off only to the road at the end the car leaves by (2026-11-22)

[src/scene/Corridor/CorridorNetwork.cpp](../src/scene/Corridor/CorridorNetwork.cpp), `CorridorNetwork::step`, ramp branch. A
C/D at GCP x CIP welds on to the next ramp while still over its from-road's shoulder; checking both end roads handed the
car back to the GCP there. The nearer end decides:

```cpp
for (int e = 0; e < 2; ++e) {
    const int rid = e ? rp.to : rp.from;
    // N5: only the road at the end the car leaves by (a C/D beside its from-road welds on over its shoulder)
    if (rid >= static_cast<int>(m_roads.size()) || (u >= 0.5 * static_cast<double>(rp.length())) != (e == 1))
        continue;
```

## N6: graph closure for the ramp grade solve (2026-11-22)

[src/scene/Corridor/CorridorNetwork.cpp](../src/scene/Corridor/CorridorNetwork.cpp), `CorridorNetwork::grade_ramps`. Every
constraint (grade between neighbours, equal heights at a weld, ramp a over ramp b by c) is a difference constraint, so
the least solution over lower bounds (up) or the greatest under upper bounds (down) is a Bellman-Ford style relaxation
to a fixpoint. Needed because a per-ramp filter with fixed welds left 48 ramps with no room; shared welds spread a
climb along the whole chain.

```cpp
const auto close = [&](Field& v, bool up) {
    const auto pull = [&](float& a, float b, float d) {
        const float c  = up ? std::max(a, b - d) : std::min(a, b + d);
        const bool  ch = std::abs(c - a) > 0.5f;  // 0.5 mm
        a              = c;
        return ch;
    };
    for (int it = 0, ch = 1; ch && it < 400; ++it) {
        ch = 0;
        for (size_t i = 0; i < R; ++i) {  // along each ramp, both ways
            const auto& P = m_ramps[i].pts;
            for (size_t k = 1; k < P.size(); ++k) ch |= pull(v[i][k], v[i][k - 1], g[i] * (P[k].u - P[k - 1].u));
            for (size_t k = P.size() - 1; k-- > 0;) ch |= pull(v[i][k], v[i][k + 1], g[i] * (P[k + 1].u - P[k].u));
        }
        for (const Cross& c : cross)  // ramp a over ramp b
            ch |= up ? pull(v[c.a][c.k], v[c.b][c.q] + c.c, 0.0f) : pull(v[c.b][c.q], v[c.a][c.k] - c.c, 0.0f);
        // ... welds: every member to the group's max (up) / min (down)
    }
};
```

## N7 soffit normals (2026-11-22)

[CorridorDiamonds.cpp](../src/scene/Corridor/CorridorDiamonds.cpp) `cmesh::soffit`: `MeshBuilder::emit_strip` always computes
up-facing normals (`c.y < 0 ? -c : c`), so swapping the edges "to face down" changed the winding but not the normal.
A downward surface needs its normal set explicitly, or the deferred lighting shades it as an up face in its own shadow:

```cpp
const bool up = glm::cross(R[i] - L[i], L[i + 1] - L[i]).y > 0.0f;  // Strip winding: (L R L1) faces along this
...
const auto v = [&](const Vec3& p) { return Vertex{p, dn, Vec2(p.x / tile, p.z / tile), tan}; };
b.push_quad(v(a), v(c), v(a1), v(c1), QuadWinding::Strip);
```
