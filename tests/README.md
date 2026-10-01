---
verified: 2026-09-29 (updated after the pass registry / dynamic-rendering follow-ups)
against: c843ebe + uncommitted refactor   # git rev-parse --short HEAD
owner: docs-scribe
---

# tests

Unit and golden tests using [Catch2 v3](https://github.com/catchorg/Catch2). Nothing here needs a GPU, a window or a Vulkan
loader, but the targets differ in what they link (all three are declared under `tests/`):

| Target | Built when | Links | Sources |
|--------|------------|-------|---------|
| `swish_tests` | always | Catch2, GLM, **GLFW** (`Camera.cpp` and `CarEntity.cpp` call `glfwGetKey`) | the 9 top-level `test_*.cpp` listed in `tests/CMakeLists.txt` (the 10th, `test_debug_params_io.cpp`, is in `swish_debug_tests`) + `Camera`, the globbed `RoadScene/*.cpp`, `CorridorAlignment`, `CorridorBounds`, `RoadGeometry`, `CarEntity`, `Entity` |
| `swish_utils_tests` | always (`tests/utils/CMakeLists.txt`) | Catch2, GLM (+ Vulkan *headers* only); **no GLFW** | 24 files `tests/utils/test_<topic>_<Header>.cpp` (plus the renderer ones below) + `src/utils/io/File.cpp`, `src/renderer/Passes/PassRegistry.cpp`, `src/renderer/Passes/ImageRegistry.cpp` (Vulkan-handle bookkeeping only, no Vulkan call) |
| `swish_debug_tests` | only with `-DSWISH_DEBUG_UI=ON` | Catch2, GLM, toml++; defines `SWISH_DEBUG_UI` | `test_debug_params_io.cpp`, `utils/test_debug_TomlGlm.cpp`, `src/debug/DebugParamsIO.cpp` |

`swish_debug_tests` is separate on purpose: defining `SWISH_DEBUG_UI` on `swish_tests` would change `CarEntity`'s layout under test.

## Build & Run

```bash
make test          # build release, then ctest (build/)
make test-debug    # build the debug-UI tree, then ctest (build-debug/): also runs swish_debug_tests

# Or by hand
cmake --build build --target swish_tests swish_utils_tests
ctest --test-dir build --output-on-failure

# Run a specific tag (binaries live under build/tests/)
./build/tests/swish_tests "[camera]"
./build/tests/swish_tests "[roadscene]"
./build/tests/utils/swish_utils_tests "[fileio]"
./build/tests/utils/swish_utils_tests "[renderer]"   # pass-order pin, bindings

# Verbose: show every assertion
./build/tests/swish_tests -v high

# Run with AddressSanitizer + UBSan
cmake -B build -DENABLE_SANITIZERS=ON
cmake --build build --target swish_tests && ./build/tests/swish_tests
```

---

## Test Coverage

| File | Tag | What it covers |
|------|-----|---------------|
| `test_camera.cpp` | `[camera]` | Forward/right/up vectors from yaw+pitch, WASD delta, sprint multiplier, bounds clamping, pitch gimbal-lock clamp |
| `test_road_scene.cpp` | `[roadscene]` | Vertex count non-zero for valid config, vertex count ≤ `max_vertex_count()`, degenerate config throws, the ordered `intro_elements()` / `chunk_elements()` tables |
| `test_road_params.cpp` | `[roadparams]` | `RoadParams.h` constants pinned against `config/road.toml` → `road.bin`; `kExit` vs generated ramp geometry; `snap_dash_to_chunk` |
| `test_corridor.cpp` | `[corridor]` | Real-world coordinate transform, compaction invariants, `CorridorAlignment` frame conversions, drivable bounds |
| `test_car_physics.cpp` | `[car]` `[wheel]` | Longitudinal force balance, traction caps, bicycle model, wet grip, AWD, wheel kinematics |
| `test_rain.cpp` | `[rain]` | Marshall-Palmer / Gunn-Kinzer drop-size and velocity models (`RainPhysics.h`) |
| `test_pbr.cpp` | `[pbr]` | `F0` from albedo / metalness, dielectric vs metal mapping |
| `test_golden_car.cpp` | `[golden]` | Real `CarEntity::update()` vs `golden/car_trajectory.csv` (4 scenarios) |
| `test_golden_mesh.cpp` | `[golden]` | `generate()`, `generate_chunk()`, `generate_interchange_scene()` vs `golden/road_mesh.txt` |
| `test_debug_params_io.cpp` | `[debugio]` `[tunables]` | (`swish_debug_tests`) preset round-trip, merge-load, the `Tunables.def` registry (12 tunables) vs the GLSL release macros, `puddleCoverage` default == `swish::road::kPuddleCoverage` == the `lighting.frag` literal |
| `utils/test_io_File.cpp` | `[fileio]` | `util::io::read_binary`: binary write + read round-trip, empty file returns zero bytes, missing file throws |

### Golden tests

`tests/golden/` holds committed fixtures (`car_trajectory.csv`, `road_mesh.txt`, `release_spv.sha256`); see
[`golden/README.md`](golden/README.md). They are the refactor safety net: **never regenerate a golden to make a refactor
pass.** `SWISH_UPDATE_GOLDENS=1` is only for a deliberate behaviour change, with a changelog entry. The intro mesh hashes
are bit-exact to **Release + Clang** codegen (they fail at `-O0` and under GCC because of FMA contraction; counts, bounds and
chunk/interchange hashes still pass), and the `.spv` hashes assume the SDK `glslc` (`VULKAN_SDK` set).

### `tests/utils/` (target `swish_utils_tests`, GLFW-free)

One `test_<topic>_<Header>.cpp` per header under `src/utils/` (plus two `test_renderer_*.cpp` for the pass registry and `Bindings.h`); links only Catch2 + GLM (the `utils/vulkan/` tests use the
Vulkan headers, never the loader). Run with `./build/tests/utils/swish_utils_tests` or `ctest`. The debug-UI-only
`utils/test_debug_TomlGlm.cpp` is built into `swish_debug_tests` instead (it needs toml++).

| Topic | Files | Tag(s) |
|-------|-------|--------|
| `units/` | `test_units_Units.cpp` | `[units]` |
| `types/` | `test_types_Types.cpp` | `[types]` |
| `math/` | `test_math_Smoothing.cpp`, `test_math_Noise.cpp`, `test_math_Hash.cpp`, `test_math_Sequences.cpp` | `[smoothing]` `[noise]` `[hash]` `[sequences]` |
| `geometry/` | `test_geometry_Frustum.cpp`, `test_geometry_Polyline.cpp` | `[frustum]` `[polyline]` |
| `color/`, `container/`, `meta/` | `test_color_Color.cpp`, `test_container_VectorOps.cpp`, `test_meta_EnumCycle.cpp`, `test_meta_Tags.cpp` (tag-object overload selection, generic visitor) | `[color]` `[container]` `[enumcycle]` `[tags]` |
| `io/` | `test_io_File.cpp` | `[fileio]` |
| `vulkan/` | `test_vulkan_Check.cpp`, `test_vulkan_Init.cpp`, `test_vulkan_PushConstants.cpp`, `test_vulkan_Barrier.cpp`, `test_vulkan_Rendering.cpp` (dynamic-rendering structs, `RenderingInfo` builder), `test_vulkan_RenderPass.cpp`, `test_vulkan_Descriptors.cpp`, `test_vulkan_Samplers.cpp`, `test_vulkan_ImageView.cpp` | `[vulkan]` |
| `renderer/` (bookkeeping only) | `test_renderer_Bindings.cpp` (`bindings.glsl` numbers), `test_renderer_PassRegistry.cpp` (**pins the pass order** from `Passes/PassList.def`, release and debug lists; registry lifecycle, `ImageRegistry`, declared sampler inputs) | `[renderer]` |
| `debug/` | `test_debug_TomlGlm.cpp` (debug-UI build only) | `[tomlglm]` |

Keep `,` `;` `[` `]` out of `TEST_CASE` names: Catch/CTest discovery splits on them.

---

## Adding a Test

<details>
<summary><strong>Step-by-step</strong></summary>

1. Engine test: create `tests/test_<feature>.cpp` and add the filename to `add_executable(swish_tests ...)` in `tests/CMakeLists.txt`. If it needs an implementation `.cpp` that is not yet linked, add it to the same list (RoadScene sources are globbed). Sources are globbed for the game, **but the test list is manual**.
2. Render-pass change (a new line in `Passes/PassList.def`): update the expected lists in `tests/utils/test_renderer_PassRegistry.cpp` in the same change.
3. Util test (a header under `src/utils/`): create `tests/utils/test_<topic>_<Header>.cpp` and add it to `add_executable(swish_utils_tests ...)` in `tests/utils/CMakeLists.txt`. No GLFW.
4. Debug-UI-only test: add it to `swish_debug_tests` in `tests/CMakeLists.txt` (inside the `if(SWISH_DEBUG_UI)` block).
5. Includes are rooted at `src/` (`#include "utils/units/Units.h"`), never `../`. Keep `,` `;` `[` `]` out of `TEST_CASE` names.
6. Follow this pattern (`RoadScene::generate()` and `RoadScene(const RoadConfig&)` are the real entry points; see `test_road_scene.cpp` for current usage):

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

static constexpr float kEps = 1e-4f;

// Build a valid default object — never inline complex construction in TEST_CASE
static RoadConfig make_test_config() {
    RoadConfig cfg{};
    cfg.magic      = RoadConfig::MAGIC;
    cfg.version    = RoadConfig::VERSION;
    cfg.lane_width = 3.7f;
    cfg.lane_count = 3;
    return cfg;
}

TEST_CASE("Road: valid config produces non-zero geometry", "[roadscene]") {
    auto cfg  = make_test_config();
    auto mesh = RoadScene::generate(cfg);
    REQUIRE(mesh.vertices.size() > 0);
    REQUIRE(mesh.vertices.size() <= RoadScene::max_vertex_count(cfg));
}

TEST_CASE("Road: zero lane count is rejected", "[roadscene]") {
    auto cfg      = make_test_config();
    cfg.lane_count = 0;
    REQUIRE_THROWS_AS(RoadScene::generate(cfg), std::invalid_argument);
}

TEST_CASE("Camera: forward vector at yaw=0 pitch=0 is (0,0,-1)", "[camera]") {
    Camera cam;
    cam.set_yaw(0.0f);
    cam.set_pitch(0.0f);
    auto fwd = cam.get_forward();
    REQUIRE_THAT(fwd.x, Catch::Matchers::WithinAbs(0.0f, kEps));
    REQUIRE_THAT(fwd.y, Catch::Matchers::WithinAbs(0.0f, kEps));
    REQUIRE_THAT(fwd.z, Catch::Matchers::WithinAbs(-1.0f, kEps));
}
```

</details>

---

## Conventions

<details>
<summary><strong>Float comparisons</strong></summary>

Never use `==` on floats. Always use `WithinAbs` with an epsilon:

```cpp
REQUIRE_THAT(value, Catch::Matchers::WithinAbs(expected, 1e-4f));
```

For values that may be large (e.g. vertex positions in world space), prefer `WithinRel`:

```cpp
REQUIRE_THAT(large, Catch::Matchers::WithinRel(expected, 1e-5f));
```

</details>

<details>
<summary><strong>What must be tested</strong></summary>

| What | Required tests |
|------|---------------|
| Pure math functions | Nominal + boundary + degenerate inputs |
| File I/O helpers | Read round-trip, missing file throws, empty file, magic/version mismatch |
| Geometry generators | Non-zero count, upper-bound count, degenerate config throws |
| Physics step | Single-step value matches analytic formula |

**Physics test example** — verify exponential drag against the analytic solution:

The drag equation $\dot{v} = -k \cdot v$ has exact solution $v(t) = v_0 \cdot e^{-kt}$.

```cpp
TEST_CASE("CarEntity drag matches analytic solution", "[car]") {
    constexpr float kDrag = 1.5f;
    constexpr float v0    = 20.0f;
    constexpr float dt    = 0.016f;   // one frame at 60 Hz

    float v = v0;
    v *= std::exp(-kDrag * dt);       // engine formula

    float expected = v0 * std::exp(-kDrag * dt);  // analytic
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(expected, 1e-5f));
}
```

</details>

<details>
<summary><strong>No Vulkan loader in tests; GLFW only in <code>swish_tests</code></strong></summary>

No test target links `Vulkan::Vulkan` (the loader) or opens a window. `swish_tests` links `glfw` only because `Camera.cpp` and `CarEntity.cpp` poll `glfwGetKey`; `swish_utils_tests` and `swish_debug_tests` do **not** link GLFW, which is the guard that keeps `src/utils/` free of it. If a class you want to test pulls in Vulkan calls (e.g. `CorridorDriver`, which needs `Renderer`), split the pure part out instead: `CorridorBounds.cpp` is exactly that split, so the tests link it without the engine. Pure helpers that need no `.cpp` belong header-only in `src/utils/<topic>/` (see [`src/utils/README.md`](../src/utils/README.md)) and get a `tests/utils/` test.

`utils/vulkan/*.h` include `<vulkan/vulkan.h>`; the utils target links only the Vulkan *headers* (`Vulkan::Headers`), so those tests call no Vulkan function.

</details>
