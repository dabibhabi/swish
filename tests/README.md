# tests

Unit tests using [Catch2 v3](https://github.com/catchorg/Catch2). Tests are pure math/I/O — they do not link Vulkan or GLFW.

## Build & Run

```bash
# Build tests only
cmake --build build --target swish_tests

# Run all tests with output on failure
cd build && ctest --output-on-failure

# Run a specific tag
./build/swish_tests [camera]

# Run with sanitizers (AddressSanitizer + UBSan)
cmake -B build -DENABLE_SANITIZERS=ON
cmake --build build --target swish_tests && ./build/swish_tests
```

## Test Coverage

| File | Tag | What it tests |
|------|-----|---------------|
| `test_camera.cpp` | `[camera]` | Free-fly movement deltas, sprint multiplier, bounds clamping |
| `test_road_scene.cpp` | `[road]` | Procedural geometry vertex counts (upper-bound), degenerate config inputs |
| `test_file_io.cpp` | `[fileio]` | Binary read/write round-trips, empty file handling, missing file errors |

## Adding a Test

1. Create `tests/test_<feature>.cpp`
2. Add it to `add_executable(swish_tests ...)` in `tests/CMakeLists.txt`
3. Use Catch2 v3 patterns:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("description", "[tag]") {
    REQUIRE(result == expected);
    REQUIRE_THAT(f, Catch::Matchers::WithinAbs(expected, 1e-4f));
}
```

4. Put shared helper builders in anonymous functions at the top of the file (e.g. `make_test_config()`).

## Conventions

- Float comparisons always use `WithinAbs` — never `==` on floats.
- Tests must compile without Vulkan/GLFW headers; use `#include` guards or separate header-only math utilities.
- Degenerate inputs (zero-size geometry, empty files) must be explicitly tested for any generator or I/O function.
