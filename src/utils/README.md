# src/utils

Generic helpers with no subsystem state, organised **by topic**. One folder per topic, one theme per header,
`PascalCase.h` file names, `snake_case` functions, namespace `swish::util::<topic>`.

*AI-generated (deep-clean wave B, 2026-09-29); hand-maintained from here on, review before relying on it.*

## Rules

| Rule | Detail |
|------|--------|
| Dependencies | `utils/` includes only the standard library and GLM (plus the Vulkan headers under `vulkan/`, toml++ under `debug/`). It never includes `core/`, `renderer/`, `scene/` or project `debug/` headers. |
| Include form | Rooted at `src/`: `#include "utils/math/Smoothing.h"`. Inside the same directory use the bare name (`#include "Check.h"`). No `../` in any include, anywhere in the repo. |
| Namespaces | `swish::util::<topic>` (`math`, `units`, `geometry`, `color`, `container`, `meta`, `io`, `debug`). Vulkan glue is `swish::vkx`. Vocabulary types stay plain `swish::` (`u32`, `Vec3`, ...), and so do the dispatch tags (`swish::tag`, `meta/Tags.h`). Use a local alias in `.cpp` files (`namespace um = swish::util::math;`); never `using namespace` in a header. |
| Header-only vs `.cpp` | Header-only unless the code pulls heavy includes (`io/File.cpp` for `<fstream>`, `io/StbImage.cpp` for stb). `.cpp` files under `src/` are globbed into `swish`, no list to edit. |
| Doc block | Every header starts with `Purpose / Units / Complexity / (Precision) / Example / See also / Test`. `vulkan/` headers use their own longer block (`Vulkan concept / Spec / ...`, see `vulkan/README.md`). |
| Tests | One `tests/utils/test_<topic>_<Header>.cpp` per header, built into `swish_utils_tests` (Catch2 + GLM only, **no GLFW**). |
| Floats | Keep expression order when moving code; goldens and release output are byte-checked. |

## Layout

| Topic | Header | Purpose |
|-------|--------|---------|
| `types/` | `Types.h` | Umbrella for the three headers below. |
| | `GlmTypes.h` | `vec2/3/4`, `mat3/4`, `quat` plus the legacy `Vec3` / `Mat4` aliases (`swish::`). |
| | `StdTypes.h` | `u8..u64`, `i8..i64`, `f32`, `f64`, `usize` (`swish::`). |
| | `GlfwTypes.h` | `glfw_window` forward declaration, no GLFW include. |
| `units/` | `Units.h` | The one definition of `1 m = 1000 WU`: `kWuPerMetre`, `kWuPerFt`, `m()`, `ft()`, `to_m()`, `to_ft()`, `kPi`, `kTwoPi`. |
| `math/` | `Smoothing.h` | `exp_smooth`, `exp_smooth_angle_deg`: framerate-independent easing. |
| | `Noise.h` | `quasi_noise`: three-sine camera-shake signal in [-1, 1]. |
| | `Hash.h` | `hash01(int)`: integer hash to [0, 1] (per-tree jitter). |
| | `Sequences.h` | `halton(index, base)`: TAA jitter sequence. |
| `geometry/` | `Frustum.h` | `extract_frustum_planes`, `sphere_outside_frustum`. |
| | `Polyline.h` | `polyline_length`, `polyline_sample` (arc-length position + tangent). |
| `color/` | `Color.h` | `rgba(const float*)` to `glm::vec4`. |
| `container/` | `VectorOps.h` | `affine(vec, k, bias)`, `filled(n, v)` for `std::vector<float>`. |
| `meta/` | `EnumCycle.h` | `CountedEnum` concept, `enum_count<E>()`, `next_enum(e)`. |
| | `Tags.h` | Tag dispatch: empty `swish::tag::flag_t` / `real_t` (objects `tag::flag`, `tag::real`) that pick an overload at compile time; `is_tag_v`. First user: `debug/Tunables.h` `visit`. |
| `physics/` | `RigidBody.h` | Generic 6-DOF rigid body (SI units): force/torque accumulators, semi-implicit Euler with angular momentum carried; `euler_yxz_from_rotation`. Stub chassis user: `scene/Entity/RigidChassis.h`. |
| `io/` | `File.h` / `File.cpp` | `read_binary(path)`: whole-file read, throws on failure. |
| | `StbImage.cpp` | The single translation unit that compiles `stb_image`. |
| `vulkan/` | `Check.h` | `VK_CHECK(expr)` + `vkResultString()`. |
| | `Init.h` | sType-prefilled struct factories + swapchain result predicates. |
| | `Barrier.h`, `Rendering.h`, `RenderScope.h`, `RenderPass.h`, `Descriptors.h`, `Samplers.h`, `ImageView.h`, `DebugLabel.h`, `DeviceHandle.h`, `PushConstants.h`, `ShaderModule.h` | Barriers, Vulkan 1.3 dynamic rendering, and the builders every pass uses. See [vulkan/README.md](vulkan/README.md). |
| `debug/` | `TomlGlm.h` | `vec3_to_toml`, `read_vec3`; the whole file is inside `#ifdef SWISH_DEBUG_UI`. |

Not utils on purpose: `MAX_FRAMES_IN_FLIGHT` lives in `renderer/RendererConfig.h`; `RainPhysics.h` and the glTF matrix
helpers stay with their domain (rationale: [docs/utils-component-design.md](../../docs/utils-component-design.md), move-table rows 26 and 27). Repo-wide rules: [docs/KEEPING_IT_CLEAN.md](../../docs/KEEPING_IT_CLEAN.md).

## Examples

```cpp
#include "utils/math/Smoothing.h"
#include "utils/units/Units.h"
#include "utils/vulkan/Check.h"

namespace um = swish::util::math;
namespace uu = swish::util::units;

fov = um::exp_smooth(fov, target_fov, 6.0f, dt);     // k in 1/s, dt in s
const float lane = uu::ft(13.0f);                    // 3962.4 WU
VK_CHECK(vkCreateBuffer(device, &info, nullptr, &buffer));
```

`VK_CHECK` throws `std::runtime_error` with file, line and the symbolic result name. Swapchain codes
(`VK_SUBOPTIMAL_KHR`, `VK_ERROR_OUT_OF_DATE_KHR`) are expected on resize: route them through the predicates in
`vulkan/Init.h` (`swapchain_needs_recreation(r)`), not `VK_CHECK`.

## How to add a util

1. Pick the topic folder (`src/utils/<topic>/`, create it if none fits) and add `Name.h` with the doc block, namespace
   `swish::util::<topic>`, `inline` / `constexpr` functions.
2. Include only std + GLM (see the dependency rule). Use rooted includes (`"utils/units/Units.h"`).
3. If it needs a `.cpp`, put it next to the header; the CMake glob picks it up. Compile it into `swish_utils_tests` too
   (add the path to `tests/utils/CMakeLists.txt`).
4. Add `tests/utils/test_<topic>_<Name>.cpp` and list it in `tests/utils/CMakeLists.txt`. Avoid `,` `;` `[` `]` in
   `TEST_CASE` names (Catch/CTest discovery splits on them).
5. Move the call sites over (keep float expression order), then build release (`SWISH_WERROR=ON`) and debug-UI and run
   `ctest`; add a row to the Layout table above and a CHANGELOG entry.
