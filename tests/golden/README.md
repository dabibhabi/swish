# tests/golden — committed golden fixtures

Safety-net fixtures for the cleanup work. A golden is a recorded "known good" output;
a test fails when the output drifts, which for a refactor means an unintended change.

| File | Produced by | Checked by |
|------|-------------|------------|
| `road_mesh.txt` | `RoadScene::generate()`, `generate_chunk(4000)`, `generate_interchange_scene()` (counts, bounds, material index totals, FNV-1a hashes of vertices/indices/draw calls/lights, positions quantised to 0.01 WU) | `tests/test_golden_mesh.cpp` |
| `car_trajectory.csv` | The real `CarEntity::update()` at dt = 1/60 over four scenarios (launch, throttle-then-brake, dry steady turn, wet steady turn), sampled every 0.5 s | `tests/test_golden_car.cpp` (tolerance 1e-3 abs + 1e-3 rel; gear exact) |
| `release_spv.sha256` | SHA-256 of the 33 release-build `.spv` shaders (C++17 baseline) | shell one-liner below |

## Toolchain-bound

All goldens depend on the compiler, libm and (for `.spv`) the shader compiler that produced them.
Mesh and trajectory goldens were generated with Apple clang on arm64 and pass in both Release and
RelWithDebInfo. A different compiler or stdlib may need regeneration; treat a failure as
"investigate first", not "just regenerate".

## Regenerate (only after deliberately changing behaviour; review the diff)

```sh
SWISH_UPDATE_GOLDENS=1 build/tests/swish_tests "[golden]"
```

This rewrites `road_mesh.txt` and `car_trajectory.csv` in place (paths are baked in via `GOLDEN_DIR`).

## Shader hashes

`release_spv.sha256` is tied to the Vulkan SDK `glslc` 1.4.321.0 at
`~/VulkanSDK/1.4.321.0/macOS/bin/glslc`, building with `-DSWISH_DEBUG_UI=OFF`. A different glslc
version will produce different SPIR-V even for identical GLSL. Verify from the repo root after a
release build (no script needed):

```sh
(cd build/shaders && shasum -a 256 -c ../../tests/golden/release_spv.sha256)
```
