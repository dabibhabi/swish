# tests

Unit tests using [Catch2 v3](https://github.com/catchorg/Catch2). Tests are pure math/I/O — they do not link Vulkan or GLFW.

## Build & Run

```bash
# Build tests only
cmake --build build --target swish_tests

# Run all tests, show output on any failure
cd build && ctest --output-on-failure

# Run a specific tag
./build/swish_tests [camera]
./build/swish_tests [road]
./build/swish_tests [fileio]

# Verbose: show every assertion
./build/swish_tests -v high

# Run with AddressSanitizer + UBSan
cmake -B build -DENABLE_SANITIZERS=ON
cmake --build build --target swish_tests && ./build/swish_tests
```

---

## Test Coverage

| File | Tag | What it covers |
|------|-----|---------------|
| `test_camera.cpp` | `[camera]` | Forward/right/up vectors from yaw+pitch, WASD delta, sprint multiplier, bounds clamping, pitch gimbal-lock clamp |
| `test_road_scene.cpp` | `[road]` | Vertex count non-zero for valid config, vertex count ≤ `max_vertex_count()` upper bound, degenerate config throws (zero lanes, zero lane width) |
| `test_file_io.cpp` | `[fileio]` | Binary write + read round-trip, empty file returns zero bytes, missing file throws, `gcount()` mismatch detected |

---

## Adding a Test

<details>
<summary><strong>Step-by-step</strong></summary>

1. Create `tests/test_<feature>.cpp`
2. Add the filename to `add_executable(swish_tests ...)` in `tests/CMakeLists.txt`
3. Follow this pattern:

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

TEST_CASE("Road: valid config produces non-zero geometry", "[road]") {
    auto cfg  = make_test_config();
    auto mesh = RoadScene::generate(cfg);
    REQUIRE(mesh.vertices.size() > 0);
    REQUIRE(mesh.vertices.size() <= RoadScene::max_vertex_count(cfg));
}

TEST_CASE("Road: zero lane count is rejected", "[road]") {
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
<summary><strong>No Vulkan/GLFW in tests</strong></summary>

Tests compile without the Vulkan SDK or GLFW. If a class you want to test pulls in Vulkan headers transitively, extract the pure-math part into a header-only utility (e.g. `src/math/BicycleModel.h`) and test that directly.

The `tests/CMakeLists.txt` explicitly does **not** link `Vulkan::Vulkan` or `glfw` — any test file that includes a Vulkan header will fail to compile, which is the intended guard.

</details>
