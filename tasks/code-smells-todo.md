# Code smells to-do

> Drafted by AI (Claude) on 2026-11-21 from a read-only audit of the whole repo: 8 audit passes plus spot checks.
> **Owner review required. Nothing has been fixed.** Nothing was built or run, so every case below is traced
> by reading the code, not observed in the app.
>
> **Status key:**
> - ✅ **verified**: re-checked against the code in the main session.
> - 🔎 **traced**: read and reasoned through by an audit agent, not re-checked.
> - ◐ **partly**: true, but smaller than first reported (the note says how).
>
> Line numbers are for commit `8c17065` plus the uncommitted P2 edit to `App.cpp`.
> Perf items live in [`perf-todo.md`](perf-todo.md) and are not repeated here.
> **Release-visible fixes** (marked 🎯) change release output: each needs an intended golden update and a changelog entry.

**Contents:**
- [Must fix](#must-fix): 6 items
- [Should fix](#should-fix): 31 items
- [Nice to have](#nice-to-have): 23 items
- [Lean summary](#lean-summary)
- [Not checked](#not-checked)

---

## Must fix

### CS-01 ✅ 🎯 Auto-exposure locks to NaN after one bright pixel
**File:** [`src/renderer/Passes/HdrFinalizePass.cpp:47-50`](../src/renderer/Passes/HdrFinalizePass.cpp)

```cpp
        const float nits =
            std::max(glm::dot(avg, Vec3(0.2126f, 0.7152f, 0.0722f)) / m_slotPreExposure[u.frameIndex], 1.0e-3f);
        const float rate = glm::clamp(1.0f - std::exp(-u.dt * std::max(u.tune.aeSpeed, 0.0f)), 0.0f, 1.0f);
        m_adaptedNits    = std::max(m_adaptedNits + (nits - m_adaptedNits) * rate, 1.0e-3f);
```

- **Case:** the HDR image is RGBA16F, so any pixel above 65504 becomes Inf. A wet-road sun glint can get there (see CS-05). The 1×1 average becomes Inf or NaN. `std::max(NaN, 1e-3f)` returns NaN (its comparison is false), so `m_adaptedNits` stays NaN forever and the screen goes black until restart.
- **Second trigger:** for two frames after a resize, `m_lumReadback` holds unzeroed memory.
- **Fix:** add the guard, then move the adaptation step into a pure `util::light` function and test it with NaN and Inf.

```cpp
const float lum = glm::dot(avg, Vec3(0.2126f, 0.7152f, 0.0722f)) / m_slotPreExposure[u.frameIndex];
if (!std::isfinite(lum)) return;  // one Inf pixel must not poison the meter forever
const float nits = std::max(lum, 1.0e-3f);
```

### CS-02 ✅ 🎯 A coasting car never stops
**Files:** [`src/scene/Entity/CarEntity.h:453`](../src/scene/Entity/CarEntity.h), used at `CarEntity.cpp:66` and `:246`; [`src/scene/Entity/Longitudinal.h:115`](../src/scene/Entity/Longitudinal.h)

```cpp
    static constexpr float kSpeedDeadZone      = 0.5f;          // compared against m_forward_speed (WU/s)
...
    const float fRoll = (vAbs > 0.05f) ? p.rollResist * p.mass * p.gravity : 0.0f;   // m/s
```

- **Case:** the dead zone is 0.5 WU/s, which is 0.5 mm/s. Rolling resistance switches off below 0.05 m/s. Release the throttle: the car slows to about 0.05 m/s, then only aero drag (about 1e-6 m/s²) acts. It creeps at about 48 WU/s forever. The wheels turn, it yaws if you hold steer, and the HUD never reads 0. The header's own comment (`:464-466`) calls 0.5 WU/s "far too tight to ever latch".
- **Fix:**

```cpp
static constexpr float kSpeedDeadZone = 0.06f * util::units::kWuPerMetre;  // just above the 0.05 m/s rolling cutoff
```

- **Test:** coast from 1 m/s for 10 s with no input, and expect `get_speed() == 0`.

### CS-03 ✅ 🎯 Signs and trees use a white texture as their normal map
**File:** [`src/renderer/MaterialDescriptors/MaterialDescriptors.cpp:153-155`](../src/renderer/MaterialDescriptors/MaterialDescriptors.cpp)

```cpp
            // Fall back to the 1×1 white "default" if a normal/roughness map is missing.
            if (!tex) { tex = textures.get_texture("default"); }
```

- **Case:** `textures/` has `sign_00..07.png` and `tree_leaves.png` but no `_normal` files. White decodes to (1,1,1), so `gbuffer.frag:114` builds `normalize(T+B+N)`, a normal tilted about 55°. Sun, headlights, IBL and SSR all hit signs and trees from the wrong angle, in release too. A flat `default_normal` is already registered at `AppInit.cpp:123`.
- **Fix:** try the suffixed default first (`default_normal`, then `default`):

```cpp
if (!tex) tex = textures.get_texture(std::string("default") + kSuffixes[b]);
if (!tex) tex = textures.get_texture("default");
```

### CS-04 ✅ 🎯 Release draws everything past about 98 m as sky
**Files:** [`shaders/lighting.frag:370-374`](../shaders/lighting.frag); the same threshold in `ssao.frag:49`, `godrays.frag:59` and `ssr.frag:55,122`

```glsl
#ifdef SWISH_DEBUG_UI
    if (depth <= 0.0) {  // only the clear value is sky: near 10 WU puts geometry past 100 m below 1e-4
#else
    if (depth < 0.0001) {
#endif
```

- **Case:** reverse-Z with near = 10 WU (`AppInit.cpp:285`) and far = 4.3e6 WU. The depth of a surface at 98 m is $\frac{10}{98000} - \frac{10}{4.3\times10^6} \approx 9.97\times10^{-5} < 10^{-4}$, so the far road and props get sky radiance and no lighting. Debug already fixed this; release did not.
- **Fix:** use `if (depth <= 0.0)` in both branches, and in the other three shaders.

### CS-05 ✅ 🎯 GGX epsilon flattens sharp highlights, and lit output has no Inf clamp
**File:** [`shaders/lighting.frag:670-671`](../shaders/lighting.frag); the same at `:214-215` (clearcoat and flake) and `:870-871` (point lights); output at `:1066`

```glsl
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom + 0.0001);
```

- **Case:** clearcoat roughness 0.045 gives a2 = 4.1e-6. At NdotH = 1, D = 0.041 instead of $1/(\pi a^2) \approx 77{,}600$, so the "mirror coat" sun glint is about 6 orders too weak. At roughness 0.2, D = 16 instead of 199.
- **Coupled with CS-01:** removing the epsilon alone overflows RGBA16F.
- **Fix:** do all three together: drop the epsilon, raise the roughness floor, clamp the output.

```glsl
float D = a2 / max(PI * denom * denom, 1e-7);   // a2 from max(roughness, 0.06)
...
outColor = vec4(min(lit_color, vec3(65000.0)), 1.0);
```

### CS-06 ✅ 🎯 Drum caps on the NYS Pavilion write NaN normals
**Files:** [`src/scene/Corridor/CorridorLandmarks.cpp:24-33`](../src/scene/Corridor/CorridorLandmarks.cpp), through `cmesh::face` at [`CorridorDiamonds.cpp:28-37`](../src/scene/Corridor/CorridorDiamonds.cpp)

```cpp
        face(b, c + e0, c + e1, t + e0, t + e1, glm::normalize(e0 + e1));
        face(b, t, t, t + e0, t + e1, kUp);
        face(b, c, c, c + e0, c + e1, -kUp);
...
    const Vec3 t = glm::normalize(p1 - p0), bt = glm::normalize(p2 - p0);   // p0 == p1 -> NaN
```

- **Case:** 3 towers × 2 drums (16 and 12 sides) gives 56 caps with NaN tangents and UVs. `gbuffer.frag` writes `normalize(TBN * n)` = NaN, which TAA and bloom can spread. `CorridorBridges.cpp:54` already warns about this trap.
- **Fix:** put the repeated point last, so the first edge is not zero length.

```cpp
face(b, t, t + e0, t + e1, t + e1, kUp);
face(b, c, c + e0, c + e1, c + e1, -kUp);
```

---

## Should fix

### CS-07 ✅ Presets silently drop about 10 tuned sliders, and a test asserts the loss
**Files:** [`src/debug/DebugParamsIO.cpp:103-207`](../src/debug/DebugParamsIO.cpp) (save) and `:221-354` (load); [`tests/test_debug_params_io.cpp:362-378`](../tests/test_debug_params_io.cpp)

```cpp
    REQUIRE(debugio::load(dst, pf.name));
    CHECK(dst.hazeDist == DebugParams{}.hazeDist);
    CHECK(dst.godrayDensity == DebugParams{}.godrayDensity);
    CHECK(dst.cullMainViewDist == DebugParams{}.cullMainViewDist);
    CHECK(dst.wheelSpinMul == DebugParams{}.wheelSpinMul);
```

- **Not saved:** `godrayDensity/Decay/Weight`, `hazeDist`, `hazeMax`, `cullMainViewDist`, `wheelSpinMul`, `sunLux`, `skyNits`, `cascadeBlend`, `lampLumens`, `retroGain`. All have sliders (`DebugUI.cpp:838-1212`).
- **Case:** tune god-ray density, Save "lie", restart, Load "lie": the value is back at its default.
- **Fix:** add one `TUNE_F` row per field to `src/debug/Tunables.def` (the registry saves it automatically), then flip the test to round-trip the values. `sunLux` and `skyNits` are also written by the time-of-day presets, so leaving those two out may be intended; decide per field.

### CS-08 ✅ Preset save truncates the old file first, and the name is not checked
**File:** [`src/debug/DebugParamsIO.cpp:209-212`](../src/debug/DebugParamsIO.cpp); name from `DebugUI.cpp:801`

```cpp
    std::ofstream f(presets_dir() + name + ".toml", std::ios::trunc);
    if (!f) return false;
    f << "# swish debug preset — edit freely; missing keys keep their in-app value\n" << tbl << '\n';
    return static_cast<bool>(f);
```

- **Case:** a full disk or a crash mid-write leaves `lie.toml` empty, and the preset is gone. A name of `../x` writes outside the presets folder. An empty name writes a hidden `.toml` that `list_presets` never shows.
- **Fix:**

```cpp
if (name.empty() || name.find_first_of("/\\") != std::string::npos || name.find("..") != std::string::npos) return false;
const std::string dst = presets_dir() + name + ".toml", tmp = dst + ".tmp";
{ std::ofstream f(tmp, std::ios::trunc); if (!(f << header << tbl << '\n')) return false; }
std::error_code ec; std::filesystem::rename(tmp, dst, ec);  // atomic replace
return !ec;
```

### CS-09 ◐ The car stays "at speed" while pinned at the corridor end or a dead end
**File:** [`src/scene/Corridor/CorridorDriver.cpp:102-150`](../src/scene/Corridor/CorridorDriver.cpp); `CarEntity::halt()` is at `CarEntity.h:75-79`

```cpp
        if (deadEnd) {
            sNew       = m_prevS;
            latClamped = m_prevLat;
        }

        if (sNew != m_station || latClamped != m_lateral) {
```

- **Case:** drive into the east end at 150 mph with the default bicycle model. Each frame the car moves on and is clamped back, so the speedo, rpm, wheel spin, FOV kick, rain lean and spray all act as if it is driving. Steering yaws it in place.
- **Fix** (after `car.set_position(pos);`):

```cpp
if (deadEnd || sNew != m_station) car.halt();  // terminus / dead end: stop, don't just pin
```

- **Test gap:** `CorridorDriver::update` has no test (it needs a `Renderer`).

### CS-10 ✅ The material-fix metalness is overwritten on the next line
**File:** [`src/scene/Entity/CarEntity.cpp:846-852`](../src/scene/Entity/CarEntity.cpp)

```cpp
        if (m_material_fixes) {
            if (s.fixMetalness >= 0.0f) dc.metalness = s.fixMetalness;
            dc.roughnessMul = s.fixRoughnessMul;
        }
        ...
        dc.metalness  = s.metalness;
```

- **Case:** turn on Material fixes for a slot with `fixMetalness`. Roughness applies; metalness never does.
- **Fix:** move `dc.metalness = s.metalness;` above the `if (m_material_fixes)` block.

### CS-11 🔎 🎯 Puddles and tyre grip jump at every origin rebase
**Files:** [`CorridorDriver.cpp:75-80`](../src/scene/Corridor/CorridorDriver.cpp) (rebase, `kRebaseQuantum` at `:27`); CPU side `CarEntity.cpp:143,446` through `WetGrip.h:142-144`; GPU side [`shaders/lighting.frag:448`](../shaders/lighting.frag) through `shaders/common/puddle.glsl:21`

```glsl
    float puddle = puddleAmount(fragWorldPos.xz, SP_PUDDLE_COVERAGE, road)
                 * smoothstep(0.05, 0.5, wetness);
```

- **Case:** the puddle noise is sampled at the shifted (render-frame) xz. Each rebase moves it by 0.13 of a noise cell, so in rain every puddle pops to a new place, and hydroplaning grip jumps with it. The free-fly camera is not shifted, so it teleports.
- **Stale comment:** `lighting.frag:203` and `:447` say puddles are "zero in release", but `SP_PUDDLE_COVERAGE` is `0.501` in release (`:153`).
- **Fix:** sample with the true position. Pass the offset in noise-cell units (computed in double on the CPU) as a uniform, and mirror it in `WetGrip.h`.

```glsl
vec2 p = worldXZ * 0.00013 + sp.puddleOffset;  // puddleOffset = vec2(0, float(-originShift * 0.00013))
```

### CS-12 🔎 🎯 Release runs physics once per frame; debug runs it at 240 Hz
**Files:** [`src/scene/Entity/CarEntity.cpp:99-108`](../src/scene/Entity/CarEntity.cpp), `src/debug/Tunables.def:133-134`

```cpp
void CarEntity::update(float dt) {
#ifdef SWISH_DEBUG_UI
    // Equal slices, not an accumulator: no leftover time, so no render interpolation is needed.
    if (const int n = substep_count(dt, m_substep_hz); n > 1) {
        for (int i = 0; i < n; ++i) step(dt / static_cast<float>(n));
        return;
    }
#endif
```

- **Case:** all tuning happens at 240 Hz. Players get one explicit Euler step of up to 1/15 s. For the 993 at 5 m/s, the lateral rate is $\lambda = \frac{C_{\alpha f}+C_{\alpha r}}{m v} \approx 32\ \text{s}^{-1}$, so a hitch gives $\lambda\,\Delta t \approx 2.15 > 2$, which is unstable: sideways jitter. `Tunables.def` says this split is deliberate.
- **Fix:** remove the `#ifdef` and give release a fixed 240 Hz. This changes the release golden trajectory on purpose.

### CS-13 🔎 "Release is byte-identical" has no automated gate
**Files:** `tests/golden/release_spv.sha256`, [`tests/CMakeLists.txt`](../tests/CMakeLists.txt); `tasks/todo.md:835` says it is a manual check

- **Case:** an unbalanced `#ifdef SWISH_DEBUG_UI` in a shader passes `make test`, `make check` and the commit hook.
- **Fix:**

```cmake
if(NOT SWISH_DEBUG_UI)
  add_test(NAME release_spv_hashes
           COMMAND shasum -a 256 -c ${CMAKE_CURRENT_SOURCE_DIR}/golden/release_spv.sha256
           WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
endif()
```

- **Caveat:** the hashes match only the SDK `glslc` (see CLAUDE.md "Traps"). Skip the test when `$VULKAN_SDK` is unset.

### CS-14 ◐ A preset with NaN poisons rendering
**Files:** [`src/debug/DebugParamsIO.cpp:345`](../src/debug/DebugParamsIO.cpp) (ssaa) and `:221` (exposure); `Renderer_Frame.cpp:38-39`

```cpp
    p.ssaaScale         = tbl["quality"]["ssaa_scale"].value_or(p.ssaaScale);
```

- **Partly:** `scaleExtent` (`PostProcessManager.cpp:206-213`) already clamps the scale to ≥ 1, so `0.0` is safe. Only `nan` gets through, because every comparison with NaN is false. `exposure = nan` has no guard at all and blackens every frame (TAA history keeps it).
- **Fix:**

```cpp
if (const float v = tbl["quality"]["ssaa_scale"].value_or(p.ssaaScale); std::isfinite(v)) p.ssaaScale = std::clamp(v, 1.0f, 2.0f);
```

  Use the same `isfinite` guard in the generic `get(..., tag::real_t, ...)`.

### CS-15 🔎 The GLB accessor ignores stride, type and bounds
**File:** [`src/scene/ModelManager/ModelManager.cpp:42-52`](../src/scene/ModelManager/ModelManager.cpp); use at `:482-490`

```cpp
const T* gltf_accessor_data(const tinygltf::Model& model, int accessor_idx) {
    const auto& acc    = model.accessors[accessor_idx];
    const auto& view   = model.bufferViews[acc.bufferView];
    const auto& buffer = model.buffers[view.buffer];
    return reinterpret_cast<const T*>(buffer.data.data() + view.byteOffset + acc.byteOffset);
```

- **Case:** `SWISH_CAR=other.glb` with interleaved vertices (`byteStride` 32), u16 UVs, or fewer UVs than positions gives a scrambled mesh or a read past the buffer. A sparse accessor (`bufferView == -1`) is UB.
- **Fix:** before returning, check `acc.bufferView >= 0`, the `componentType`, `acc.ByteStride(view) == sizeof(T)` (or 0), and `offset + count * stride <= buffer.data.size()`. On failure, log and skip the primitive.

### CS-16 🔎 Texture upload never checks the pixel buffer size; 16-bit PNGs become garbage
**Files:** [`src/renderer/TextureManager/TextureManager.cpp:141-145`](../src/renderer/TextureManager/TextureManager.cpp) (debug mip builder at `:125`); [`ModelManager.cpp:148-149`](../src/scene/ModelManager/ModelManager.cpp)

```cpp
    VkDeviceSize imageSize = chain->size();
    GpuBuffer staging = gpu::hostVisibleBuffer(m_allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    memcpy(staging.mapped(), chain->data(), static_cast<size_t>(imageSize));
```

- **Case:** a 16-bit PNG in a `.glb` has `w*h*8` bytes but is uploaded as RGBA8, so the texture is garbage. The debug mip builder also reads the wrong half of the buffer.
- **Fix:** in ModelManager, `if (img.bits != 8) return "";`. In `upload_texture`:

```cpp
if (w == 0 || h == 0 || pixels.size() != size_t{w} * h * 4)
    throw std::runtime_error("TextureManager: bad pixel buffer for " + name);
```

### CS-17 🔎 Sizes duplicated between C++ and GLSL with nothing pinning them
**Files:** [`PostProcessManager.h:27`](../src/renderer/PostProcessManager/PostProcessManager.h); [`shaders/common/lights.glsl:31-36`](../shaders/common/lights.glsl) and `lighting.frag:14` against `src/scene/SceneTypes.h:73,121,132,142`

```cpp
static constexpr uint32_t PP_MAX_FRAMES = 2;
```

- **Case:** set `MAX_FRAMES_IN_FLIGHT = 3`: `m_hdrImages[2]` and `m_slotPreExposure[2]` are read and written out of bounds. Raise `MAX_SPOT_LIGHTS` to 48: the C++ `LightsUBO` grows while the GLSL offsets don't, so the light counts are read from spot data. MoltenVK gives no error.
- **Fix:** `static constexpr uint32_t PP_MAX_FRAMES = MAX_FRAMES_IN_FLIGHT;`. Move the light counts into `bindings.glsl` (`Bindings.h` already re-exports it) and add `STATIC_REQUIRE`s in `test_renderer_Bindings.cpp`.

### CS-18 🔎 One-time command buffer code copied 8 times; 3 copies ignore errors
**Files:** [`SceneGeometry.cpp:33-62`](../src/renderer/SceneGeometry/SceneGeometry.cpp), [`SpraySystem.cpp:262-287`](../src/renderer/SpraySystem/SpraySystem.cpp), [`TaaPass.cpp:309-335`](../src/renderer/TaaPass/TaaPass.cpp) (unchecked); `WindshieldRainPass.cpp:~543`, `PostProcessManager_Images.cpp:237-294`, `ResourceManager.cpp:15-70` and `75-118` (checked); helpers at `IBLManager.cpp:51,66`

```cpp
    vkEndCommandBuffer(cmd);
...
    vkQueueSubmit(s.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(s.graphicsQueue);
```

- **Case:** if a submit fails (device lost, out of memory), the staging buffer is freed and the GPU buffer holds garbage, which is then drawn with no error.
- **Fix:** move `beginOneTime`/`endOneTime` (which use `VK_CHECK`) from IBLManager into `utils/vulkan/` and call them from all 8 sites. About −100 lines.

### CS-19 🔎 🎯 `pow(1 - x, 5)` where x can round above 1
**Files:** [`shaders/lighting.frag:679`](../shaders/lighting.frag), also `:219, 268, 744, 750, 875, 932, 988, 1004`; `windshield_rain.frag:322`

```glsl
    vec3 F_sun = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
```

- **Case:** with the sun behind the camera (V ≈ L), `dot` can come out as 1.0000001, so `pow(-1e-7, 5.0)` is undefined in SPIR-V and MSL. With fast math that is `exp2(5*log2(neg))`, which is NaN. Bloom then spreads it into a black block.
- **Fix:** `pow(max(1.0 - HdotV, 0.0), 5.0)` at every site, or a shared `schlick5(x)` in `shaders/common/`.

### CS-20 🔎 Windshield sun glint mixes tangent and world space
**File:** [`shaders/windshield_rain.frag:325-328`](../shaders/windshield_rain.frag) (also `:292`)

```glsl
    vec3  Vw  = normalize(camera.camPos.xyz - fragWorldPos);
    vec3  Lw  = normalize(camera.sunDir.xyz);
    vec3  Hw  = normalize(Vw + Lw);
    float sun = pow(max(dot(dropN, Hw), 0.0), 96.0) * camera.sunColor.a * 0.5 * coverage;
```

- **Case:** `dropN` is in mesh-UV tangent space. Flat glass gives (0,0,1), so the glint peaks when the half-vector points along world +Z, which has nothing to do with the glass. `:292` uses UV-space `dropN.xy` as a screen-UV offset, and the comment at `:210` says V is inverted, so vertical refraction is mirrored.
- **Fix:** `vec3 dropW = normalize(TBN * dropN);` with a TBN built from `fragNormal` and a tangent; use `dropW` for the glint.

### CS-21 🔎 🎯 Bloom blur skips every odd texel
**File:** [`shaders/bloom_blur.frag:24-30`](../shaders/bloom_blur.frag)

```glsl
    result += texture(inputTex, fragUV - 6.0 * dir).rgb * 0.0044;
    result += texture(inputTex, fragUV - 4.0 * dir).rgb * 0.0540;
    result += texture(inputTex, fragUV - 2.0 * dir).rgb * 0.2420;
    result += texture(inputTex, fragUV              ).rgb * 0.3991;
```

- **Case:** whole-texel offsets land on texel centres, so bilinear filtering never blends neighbours. A 1-texel highlight at an odd offset is skipped, and bloom flickers as bright pixels move.
- **Fix:** use offsets `1, 2, 3` with the same weights, or true linear-sampling offsets `±1.3846` / `±3.2308` with weights `0.3162` / `0.0703`.

### CS-22 ✅ Read-only depth uses `STORE_OP_DONT_CARE`, but later passes read it
**Files:** [`src/renderer/Passes/ForwardPass.h:29-33`](../src/renderer/Passes/ForwardPass.h) (Rain, SprayDraw, Glass), [`VelocityPass.cpp:179-183`](../src/renderer/VelocityPass/VelocityPass.cpp), `WindshieldRainPass.cpp:417`

```cpp
    d.depth        = vkx::AttachmentDesc{.format  = depthFormat,
                                         .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                                         .store   = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                         .initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
```

- **Case:** by the spec, depth is undefined after these passes, yet SSR, god-rays, DoF, Glass, Windshield and TAA sample it afterwards. MoltenVK maps this to `MTLStoreActionDontCare`. It works today by luck.
- **Fix:** `.store = VK_ATTACHMENT_STORE_OP_NONE,  // core 1.3: preserved, no write`

### CS-23 🔎 Spray particle buffer has no barrier between frames
**File:** [`src/renderer/SpraySystem/SpraySystem.cpp:142-148`](../src/renderer/SpraySystem/SpraySystem.cpp)

```cpp
void SpraySystem::record_compute(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (!m_active) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    ...
    vkCmdDispatch(cmd, kSprayMaxParticles / 64, 1, 1);
```

- **Case:** one `m_particleBuffer` is shared by both frames in flight. Frame N+1's compute pass writes it while frame N's vertex shader may still be reading it, with no memory dependency. Sync validation would flag it; Metal hazard tracking may hide it.
- **Fix:** add a barrier before the dispatch.

```cpp
VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                   VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
```

### CS-24 ✅ A failed HDRI load keeps the previous sky instead of falling back
**Files:** [`src/renderer/IBLManager/IBLManager.cpp:352-354`](../src/renderer/IBLManager/IBLManager.cpp), [`WeatherController.cpp:137-142`](../src/renderer/WeatherController/WeatherController.cpp)

```cpp
    if (d->rgba.empty()) return false;
    uploadEquirect(*d, path);
    m_hdriLoaded = true;
```

- **Case:** cycle the weather to a preset whose `.hdr` is missing. `m_hdriLoaded` stays `true` from the previous preset, so `maybe_rebake_ibl` and `bake` both exit early. The cubes keep the old sky while the sun changes, and the documented procedural fallback works only on first start.
- **Fix:** `if (d->rgba.empty()) { m_hdriLoaded = false; return false; }`

### CS-25 🔎 Validation layer requested without checking it exists
**File:** [`src/renderer/VulkanContext/VulkanContext.cpp:109-112`](../src/renderer/VulkanContext/VulkanContext.cpp)

```cpp
    if (kEnableValidationLayers) {
        createInfo.enabledLayerCount   = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayer;
```

- **Case:** a `-DCMAKE_BUILD_TYPE=Debug` build on a Mac with Homebrew MoltenVK but no LunarG SDK fails at startup with `VK_ERROR_LAYER_NOT_PRESENT` ("failed to create Vulkan instance!").
- **Fix:** check `vkEnumerateInstanceLayerProperties` first. If the layer is missing, skip it, the debug-utils extension and the messenger, with one warning.

### CS-26 ✅ Cascade fit uses the TAA-jittered projection (TAA on only)
**File:** [`src/renderer/GeometryProducer/GeometryProducer.cpp:142`](../src/renderer/GeometryProducer/GeometryProducer.cpp) (radius at `:170-172`); jitter at `Camera.cpp:163-168`

```cpp
    const Mat4 invVP = glm::inverse(camera.get_projection_matrix() * camera.get_view_matrix());
```

- **Case:** with TAA on, the far-split corners move by about 100–200 WU per frame, so `radius`, the texel size and the snap grid change every frame, and shadow edges shimmer. TAA is off by default.
- **Fix:** `camera.get_projection_matrix_unjittered()`.

### CS-27 🔎 🎯 The last mainline tile stops up to 5 m short of the end
**File:** [`src/scene/Corridor/CorridorScene.cpp:118-124`](../src/scene/Corridor/CorridorScene.cpp)

```cpp
            const int          nSta    = std::max(2, static_cast<int>(std::llround((te - ts) / sp)) + 1);
            ...
            for (int i = 0; i < nSta; ++i) {
                const double                    s  = std::min(ts + static_cast<double>(i) * sp, te);
```

- **Case:** `te = main_end()` is not a multiple of the 10 m spacing (`CorridorEnds.cpp:42` uses `sqrt`). A 123.4 m last tile gives 13 stations ending at 120 m. The lane, shoulder and verge bands stop 3.4 m short, while the median and dashes reach `te`, leaving a hole where the roundabout meets the road.
- **Fix:** `std::max(2, static_cast<int>(std::ceil((te - ts) / sp - 1e-6)) + 1)`

### CS-28 🔎 `banks[i]` read from an empty vector (debug, flat asset, wide median)
**File:** [`src/scene/Corridor/CorridorScene.cpp:141-147`](../src/scene/Corridor/CorridorScene.cpp)

```cpp
                    for (size_t i = 0; i < c.size(); ++i)
                        c[i] += Vec3(-tan[i].z * sg * wide[i], -banks[i] * sg * wide[i], tan[i].x * sg * wide[i]);
```

- **Case:** `banks` is filled only when `graded`. Load a `VERSION_FLAT` asset in debug with `edgeWideMedianM` > 0 and this reads an empty vector: UB.
- **Fix:** `(banks.empty() ? 0.0f : -banks[i] * sg * wide[i])`

### CS-29 🔎 Both gizmos react to one drag
**File:** [`src/debug/DebugUI.cpp:1338-1377`](../src/debug/DebugUI.cpp)

```cpp
        ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::ROTATE, ImGuizmo::WORLD, &display[0][0]);
...
        ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::ROTATE_Z, ImGuizmo::LOCAL, &m[0][0], &delta[0][0]);
        if (ImGuizmo::IsUsing()) {
```

- **Case:** both share ImGuizmo's default ID. Dragging the sun ring makes `IsUsing()` true for the steering gizmo too, so `steerOverride` turns on and the steering jumps.
- **Fix:**

```cpp
ImGuizmo::PushID(1); ImGuizmo::Manipulate(/* sun */); ImGuizmo::PopID();
ImGuizmo::PushID(2); ImGuizmo::Manipulate(/* steer */); const bool using_ = ImGuizmo::IsUsing(); ImGuizmo::PopID();
if (using_) { /* ... */ }
```

### CS-30 ✅ The same checkbox label twice in one window
**File:** [`src/debug/DebugUI.cpp:843`](../src/debug/DebugUI.cpp) and `:866`

```cpp
                ImGui::Checkbox("Lamps and car lights on", &p.lampsOn);
...
            ImGui::Checkbox("Lamps and car lights on", &p.lampsOn);
```

- **Case:** `CollapsingHeader` doesn't push an ID, so both share one ID. With both headers open, clicking the second toggles the first (or twice), and ImGui reports an ID conflict.
- **Fix:** `"Lamps and car lights on##l6"` on line 866, or delete one of them.

### CS-31 🔎 A partial Overpass result replaces the cached OSM extract
**File:** [`tools/lie_corridor.py:120-126`](../tools/lie_corridor.py) (same in `queens_network.py:82-90`)

```python
            with urllib.request.urlopen(req, timeout=300) as resp, open(out + ".part", "wb") as fh:
                shutil.copyfileobj(resp, fh)
            os.replace(out + ".part", out)
            return
```

- **Case:** a server-side timeout still returns HTTP 200 with partial data and a `remark`. `fetch --force` replaces the good extract, and `bake` then writes a committed `.bin` with roads missing.
- **Fix:** before `os.replace`:

```python
with open(out + ".part", "rb") as fh: tail = fh.read()[-4096:]
if b"remark" in tail: raise RuntimeError(f"Overpass returned a partial result: {tail[-300:]!r}")
```

### CS-32 🔎 `gen_asphalt_fresh.py` checks its output after overwriting the committed textures
**File:** [`tools/gen_asphalt_fresh.py:34-38`](../tools/gen_asphalt_fresh.py)

```python
Image.fromarray((fill(rgb) * 255).astype(np.uint8)).save(OUT + ".jpg", quality=92)
...
assert red.mean() < 0.25, "stain mask ate the texture"
```

- **Fix:** move the `assert` up to just after `soft = ...` (line 21).

### CS-33 🔎 `shader-tools.sh` silently compiles nothing without ripgrep
**File:** [`tools/shader-tools.sh:21`](../tools/shader-tools.sh) (used at `:27`, `:30`, `:42`)

```sh
has_entry() { rg -q '(^|[[:space:]])void[[:space:]]+main[[:space:]]*\(' "$1"; }
```

- **Case:** with no `rg`, it exits 127, so every file is "SKIP" and the VS Code compile task exits 0.
- **Fix:** use `grep -Eq` instead of `rg -q` (here and on line 27).

### CS-34 ✅ The old road system is still compiled (lean, about −2,400 lines)
**Files:**
- [`src/scene/RoadScene/`](../src/scene/RoadScene/): `RoadScene.{h,cpp}`, `RoadScene_Surfaces/Furniture/Ramps.cpp`, 1,828 lines
- [`src/scene/Treadmill/`](../src/scene/Treadmill/): 213 lines
- `RoadGeometry::drivable_bounds/ribbon_length/ribbon_sample` (`RoadGeometry.cpp:51-86`)
- the `intro_exit_*` helpers

- **Case:** nothing in `src/` calls `RoadScene::` (grep-checked); only `test_golden_mesh` and `test_road_scene` use it. It shares `RoadParams.h`, so every road edit risks breaking dead code.
- **Fix:**
  1. grep for any shared type the corridor still uses (e.g. `SceneData`);
  2. point `CorridorScene.cpp:11` at `MeshBuilder.h`;
  3. delete the files and trim the two tests.

### CS-35 🔎 Unused renderer parts (lean, about −150 lines)
- **Unused HDR pass and getters:** `m_hdrRenderPass`, `m_hdrFramebuffers` and the `get_hdr_*` getters ([`PostProcessManager.h:144,150,200-203`](../src/renderer/PostProcessManager/PostProcessManager.h), created at `_Passes.cpp:38` and `_Images.cpp:175`) have no callers.

  ```cpp
      VkRenderPass get_hdr_render_pass() const { return m_hdrRenderPass; }
      VkFramebuffer get_hdr_framebuffer(uint32_t frameIndex) const { return m_hdrFramebuffers[frameIndex]; }
  ```

- **Unused `extent` parameter and pipelines rebuilt on resize:** `create_graphics(..., VkExtent2D extent)` ([`Pipeline.cpp:12-13`](../src/renderer/Pipeline/Pipeline.cpp)) never reads `extent` (viewport and scissor are dynamic). `PostProcessManager::recreate` still rebuilds every pipeline (`PostProcessManager.cpp:88`), re-translating shaders to Metal on each resize. Drop the parameter and the `createPipelines()` call in `recreate()`.
- **`m_lightsDirty`** ([`CameraUniforms.h:31,90`](../src/renderer/CameraUniforms/CameraUniforms.h), `.cpp:128`): written, never read. Delete it.
- **Depth-only pipeline copy:** `DepthOnlyPipeline.cpp:44-50,165` hand-copies `create_graphics` only to set depth bias, and skips the pipeline cache. Add `depthBias`, `biasConst` and `biasSlope` to `PipelineConfig` and call `create_graphics`.

### CS-36 ✅ ImGuizmo is fetched from `master`
**File:** [`CMakeLists.txt:101`](../CMakeLists.txt)

```cmake
        GIT_TAG        master
```

- **Fix:** pin it to a commit SHA, so a fresh `build-debug` can't break on its own.

### CS-37 ◐ The debug app bundle is missing `time_of_day.toml`
**File:** [`scripts/package_mac.sh:52`](../scripts/package_mac.sh)

```bash
cp "$build_dir/config/car.toml" "$build_dir/config/road.bin" "$res/config/"
```

- **Partly:** only `VARIANT=debug` reads it (`WeatherController.cpp:194-199` is debug-gated). That bundle falls back to golden hour for every slot.
- **Fix:** `[ "$variant" = debug ] && cp "$build_dir/config/time_of_day.toml" "$res/config/"`

---

## Nice to have

### CS-38 🔎 Frame time stored as `float`
**File:** [`src/core/App/App.cpp:90,104,143-146`](../src/core/App/App.cpp); signature at `App.h:152`

```cpp
    float current_time = static_cast<float>(glfwGetTime());
    float delta_time   = current_time - last_frame_time;
```

- **Case:** after about 2.3 hours the precision is about 1 ms; after about 36 hours, frames alternate between 0 and 31 ms.
- **Fix:** keep `last_frame_time` as a `double` (`App.h:152` → `double&`) and cast only the difference.

### CS-39 🔎 Polyline tangent is NaN on a zero-length segment
**File:** [`src/utils/geometry/Polyline.h:41-46`](../src/utils/geometry/Polyline.h)

```cpp
            float u = (seg > 1e-4f) ? std::min(s / seg, 1.0f) : 0.0f;
            pos     = pts[i - 1] + (pts[i] - pts[i - 1]) * u;
            tan     = glm::normalize(pts[i] - pts[i - 1]);
```

- **Fix:** `if (seg <= 1e-4f && i < pts.size() - 1) continue;`, plus one test with duplicate points.

### CS-40 🔎 Corridor asset not validated: divide by zero, `.front()` on an empty vector
**Files:** [`CorridorAlignment.cpp:51-57`](../src/scene/Corridor/CorridorAlignment.cpp) (also `:63`, `:69`), `CorridorAsset.h:239`, [`CorridorBounds.cpp:43`](../src/scene/Corridor/CorridorBounds.cpp)

```cpp
    const double i  = std::floor(s / sp);
...
    const DriveSpan sp = drive_spans(*m_al, {}, s, side, kLaneInset).front();
```

- **Fix:** in the loader, `if (!(h.stationSpacing > 0.f)) throw ...`. In bounds:

```cpp
const auto v = drive_spans(*m_al, {}, s, side, kLaneInset);
if (v.empty()) { minLat = maxLat = 0.0f; return; }
```

### CS-41 ◐ Bloom, AO and god-ray sizes are not clamped to ≥ 1
**Files:** [`PostProcessManager.cpp:28-30`](../src/renderer/PostProcessManager/PostProcessManager.cpp) and `:80-82` (compare with `BloomPyramidPass.cpp:179`)

```cpp
    m_bloomExtent   = {m_renderExtent.width / 4, m_renderExtent.height / 4};
    m_aoExtent      = {m_renderExtent.width / 2, m_renderExtent.height / 2};
```

- **Partly:** this only fails for a window under 4 px (bloom) or 2 px (AO).
- **Fix:** `std::max(1u, …)` in one helper used at both sites.

### CS-42 ◐ `test_source_registry.sh` regex can't see `${CMAKE_SOURCE_DIR}/src/…`
**File:** [`scripts/test_source_registry.sh:13`](../scripts/test_source_registry.sh)

```bash
if grep -qE '^\s+src/.*\.cpp\s*$' "$CMAKE"; then fail "hand-listed .cpp found in CMakeLists.txt"; fi
```

- **Partly:** widening it would flag the deliberate `swish_scene_core` list (`CMakeLists.txt:223-233`).
- **Fix:** assert the executable line instead.

```bash
grep -qE '^add_executable\(swish \$\{SWISH_SOURCES\}\)' "$CMAKE" || fail "swish exe no longer built from SWISH_SOURCES"
```

### CS-43 ✅ Small dead code (lean, about −250 lines)
| Where | What | Action |
|---|---|---|
| `src/utils/math/Angle.h` + `tests/utils/test_math_Angle.cpp` | no includer in `src/` | delete + CMake/README rows |
| `src/utils/vulkan/DebugLabel.h` | no `ScopedLabel` user (grep: only READMEs) | delete or wire it up |
| `scripts/prune_sources.sh`, `Makefile:114-115` | prints "nothing to prune" | delete |
| `Camera.cpp:45,53-55`, `Camera.h:44-47` | `set_target/get_target/set_up/get_up` no callers | delete |
| `Powertrain.h:170-172` | `GearEvent/GearAction/GearTransition` forward decls, no uses | delete |
| `CarEntity.cpp:377-378` | `arb_*` set, then overwritten at `:380` | delete |
| `shaders/common/atmosphere.glsl:93-97` | `atmTransmittance` no caller | delete |
| `shaders/dof.frag:53-55` | `#else` branch, but DoF is debug-only | delete |
| `src/utils/fsm/Fsm.h` + test (500 lines) | "no user yet" | owner's call (tutorial) |

The literal `1609344.0` appears at `CorridorDriver.cpp:271` (twice), `:279`, and `AppInit.cpp:154-155`. Add `constexpr double kWuPerMile = 1609344.0;` to `Units.h` and use it.

### CS-44 🔎 Cascade math breaks when shadow far ≤ camera near
**File:** [`GeometryProducer.cpp:128-129`](../src/renderer/GeometryProducer/GeometryProducer.cpp), `:195`

```cpp
    shadowFar              = std::min(shadowFar, camFar);
    const float shadowNear = camNear;
```

- **Case:** `csm_far` from TOML or a ctrl-clicked slider is unclamped. If it equals near, `radius = 0` and `ortho(0,0,…)` produces NaN matrices.
- **Fix:** `shadowFar = std::clamp(shadowFar, camNear * 2.0f, camFar);` and `radius = std::max(radius, 1.0f);`

### CS-45 🔎 Shadow map size is defined twice
**File:** [`GeometryProducer.cpp:121`](../src/renderer/GeometryProducer/GeometryProducer.cpp) against `PostProcessManager.h:288`

```cpp
    constexpr uint32_t pp_shadow_dim = 2048;       // PostProcessManager::kShadowDim (one cascade's resolution)
```

- **Fix:** pass `pp.get_shadow_cascade_dim()` into `compute_cascades`, or make `kShadowDim` public.

### CS-46 🔎 Debug mip chain averages sRGB bytes
**File:** [`TextureManager.cpp:132-134`](../src/renderer/TextureManager/TextureManager.cpp)

```cpp
                    mips.push_back(static_cast<uint8_t>((px(2 * x, 2 * y) + px(2 * x + 1, 2 * y) +
                                                         px(2 * x, 2 * y + 1) + px(2 * x + 1, 2 * y + 1) + 2) /
                                                        4));
```

- **Case:** high-contrast albedo (mortar, lane paint) darkens with distance. Normal-map mips are not renormalized.
- **Fix:** for `R8G8B8A8_SRGB`, convert to linear, average, then convert back.

### CS-47 🔎 Velocity pass has a stale previous view-projection after TAA is re-enabled
**File:** [`VelocityPass.cpp:40-44`](../src/renderer/VelocityPass/VelocityPass.cpp)

```cpp
    if (!u.tune.taaEnabled || !u.camera) return;
    m_prevViewProj = m_lastUnjit;
    m_lastUnjit    = u.camera->get_projection_matrix_unjittered() * u.camera->get_view_matrix();
```

- **Fix:** track it every frame, and seed with the current matrix on the first frame: `m_prevViewProj = m_hasPrev ? m_lastUnjit : vp;`.

### CS-48 🔎 Debug shader NaN sources
**Files:**
- Rain fog: [`lighting.frag:1052`](../shaders/lighting.frag) calls `normalize(vec3(toFrag.x, 0.08 * length(toFrag.xz), toFrag.z))`, which is NaN straight below the camera. Fix: `+ 1e-3` on the y term.
- Slope bias: `lighting.frag:573-574` calls `sqrt(1.0 - ndl * ndl)`, which is NaN at `ndl = 1+ulp`. Fix: `sqrt(max(1.0 - ndl * ndl, 0.0))`.

### CS-49 🔎 Constants and helpers copied between shaders or C++
- `kRingUV = 0.85` appears in both `rain_splash.vert:26` and `rain_splash.frag:19`.
- The rain tint `vec3(0.72, 0.82, 0.97)` appears in both `rain.frag:66` and `rain_splash.frag:33`.
- `WU_PER_M = 1000.0` (`rain.vert:22`) and `9.81 * 1000.0` (`rain_splash.vert:27`) re-type `kWuPerMetre`. Use `1.0 / camera.weather.w`.
- The Poisson disk in `dof.frag:25-34` duplicates `POISSON16` in `lighting.frag:169-177`.
- The sin-dot hash is copied in `gbuffer.frag:77-79`, `lighting.frag:388` and `:535`.
- **Fix:** move them into `shaders/common/hash.glsl` and a new `shaders/common/rain.glsl`.

### CS-50 🔎 Comments and docs that contradict the code
- `lighting.frag:203` and `:447` say puddles are "zero in release", but release coverage is `0.501` (`:153`).
- `composite.frag:28` says the padding is "never read", but `:132` reads `params._pad3`.
- `lighting.frag:1057` mixes `SP_FOG_COLOR` without `* skyScale`. It is harmless only because the release `SP_FOG_MAX` is 0.
- `CorridorLamps.h` calls `lamp_posts_along` a "stub", but the body is complete.
- `CLAUDE.md:91`, `shaders/README.md:66`, `docs/RECIPES.md:49-50` and `docs/data-types.md:257` say `SceneParamsUBO` has 11 rows. The code has 53 rows (848 B) (`SceneParamsUniform.h:49`).

### CS-51 🔎 Small corridor magic numbers
- **Gantry lamp clearance:** `CorridorSigns.cpp:323` uses `m(1.0f)` for the median column centre, which really is `kSignOff + kTrussSide/2` (`:80`, 0.985 m today). Use the expression instead of the literal.
- **Sodium cone mismatch:** `CorridorDressing.cpp:318-327` computes candela over a 70° half-angle but lights out to `cosOuter = cos(75°)`. Use `l.cosOuter = std::cos(half);`.
- **Lab time-of-day index:** `LightingLab.cpp:276-277` hard-codes `5` for Midnight. Use `static_cast<int>(TimeOfDay::Midnight)`.
- **Roundabout wall normal:** `CorridorEndsMesh.cpp:296-297` takes a 1 WU finite difference at up to 6e5 WU, which wobbles the normal by up to 3.5°, then zeroes `n.y` after normalizing. Use `Vec3 d = R(r + m(1.0f), am, 0) - R(r, am, 0); d.y = 0; n = glm::normalize(d) * out;`.

### CS-52 ✅ Two sun-azimuth sliders with different ranges
**File:** [`src/debug/DebugUI.cpp:837`](../src/debug/DebugUI.cpp) against `:1075`

```cpp
                ImGui::SliderFloat("Sun azimuth (rad)", &p.sunAzimuth, -kPi, kPi * 2.0f);
...
            ImGui::SliderFloat("Azimuth", &p.sunAzimuth, -kPi, kPi);
```

- **Case:** the default 250° (4.36 rad) snaps to π when the second slider is touched, so the sun swings about 70°.
- **Fix:** use `-kPi, kPi * 2.0f` on line 1075.

### CS-53 🔎 "Print values" leaves out two registry groups
**File:** [`DebugUI.cpp:449-468`](../src/debug/DebugUI.cpp)

- **Fix:** add `dump_tunable_group(p, "wheels"); dump_tunable_group(p, "gauges");`.

### CS-54 🔎 `sign_corridor.py` reads garbage when the EXIT section is missing
**File:** [`tools/sign_corridor.py:77-80`](../tools/sign_corridor.py)

```python
    i = d.find(struct.pack("<I", EXIT_TAG))
    n = struct.unpack_from("<I", d, i + 4)[0]
```

- **Fix:** `assert i >= 0, f"{path}: no EXIT section — re-run lie_corridor.py bake"`

### CS-55 🔎 Telemetry CSV export can overwrite itself
**File:** `src/debug/TelemetryRing.cpp:120-132`

- **Case:** file names have 1-second resolution, so two exports in the same second collide. The folder is relative to the current directory.
- **Fix:** add milliseconds or a counter to the name, and write under `presets_dir()`'s parent.

### CS-56 🔎 Gizmos drawn in the controls window's draw list
**File:** `DebugUI.cpp:1340-1342`

- The gizmos are projected with the game camera but drawn into the 560×1000 controls window, so the handles look stretched. Not confirmed; check in the app.

### CS-57 🔎 `smoothstep` with edge0 > edge1
**Files:** `windshield_rain.frag`, `spray.frag` (many sites)

- The spec says this is undefined; it works on Apple GPUs today. Fix: write `1.0 - smoothstep(lo, hi, x)`.

### CS-58 🔎 `octEncode` maps (0,0,−1) to +Z
`sign(0) = 0` in the octahedral normal encode. This is improbable for real normals. Fix: `signNotZero()`.

### CS-59 🔎 Rain time is a `float` that grows forever
`rain.vert` computes `vel * time`, which loses precision after hours of play. Fix: wrap `m_time` with `fmod` at a multiple of the fall period on the CPU.

### CS-60 🔎 `crest()` in Bridges and the Diamonds stub profile duplicate logic
`CorridorBridges.cpp` and `CorridorDiamonds.cpp` compute the same crest with different guards. No failing case is confirmed. Merge them into one helper when either changes.

---

## Lean summary

| Item | Lines |
|---|---|
| CS-34 old road system | about −2,400 (src + tests) |
| CS-35 renderer leftovers | about −150 |
| CS-18 one-time cmd helper | about −100 |
| CS-43 small dead code | about −250 (−750 with Fsm) |
| CS-49 shader duplicates | about −40 |
| **Total** | **about −2,900 lines, −0 dependencies** |

## Suggested order
1. **Release-visible correctness:** CS-04 (sky past 98 m), CS-01 + CS-05 (NaN exposure / GGX, together), CS-03 (normals), CS-02 (coasting), CS-06 (NaN caps).
2. **Data loss:** CS-07, CS-08, CS-14.
3. **Cheap one-liners:** CS-10, CS-24, CS-30, CS-52, CS-36, CS-37, CS-22, CS-19.
4. **Gates:** CS-13 (release `.spv` test), CS-17 (static checks).
5. **Lean sweep:** CS-34, CS-35, CS-18, CS-43.

Gates for each fix: `make test` + `make test-debug` green, 0 warnings, a changelog entry, and the owner checks in the app for anything marked 🎯.

## Not checked
- **Not read line by line:**
  - `CorridorBridges.cpp` (most of it), `CorridorDiamonds.cpp` 190-691, `CorridorDressing.cpp` 336-521, `CorridorEndsMesh.cpp` 90-225 and 300-404
  - `spray_sim.comp`, `glass.*`, `windshield_wetness.frag`, the `ibl_*` shaders
  - `SyncObjects`, `Swapchain`, `PipelineRebuild`
  - the Python tools beyond file and network handling, and `car_analyzer`
- **Not done:** no build, no app run, no validation-layer or sync-validation run. CS-22 and CS-23 may be masked by Metal's hazard tracking.
