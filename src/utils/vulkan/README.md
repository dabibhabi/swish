# src/utils/vulkan (`swish::vkx`)

Thin, documented helpers over the raw Vulkan C API, all in one namespace `swish::vkx`. Include them
rooted at `src/`, e.g. `#include "utils/vulkan/Check.h"`. Design: `archive/local/review_2026-09-29/docs/vulkan-dispatch-and-utils.md`
(local-only folder, gitignored: it exists only on the machine that produced it). Callers today: `Check.h`, `Init.h`,
`Barrier.h`, `Rendering.h` (TaaPass, Pipeline), `ShaderModule.h`, `PushConstants.h`, and (refactor R3, 2026-09-29) `RenderScope.h`,
`RenderPass.h`, `Descriptors.h`, `Samplers.h`, `ImageView.h` (every pass and `PostProcessManager`); `DebugLabel.h`
and `DeviceHandle.h` are not called yet (verified by grep 2026-09-29); `PushConstants.h` (`push_range<T>`) is used for every push range. Repo-wide context: [docs/CLEANUP_2026-09-29.md](../../../docs/CLEANUP_2026-09-29.md).

*AI-generated, review before relying on it.*

Rules: header-only; no renderer include (limits and services come in as parameters or a template
parameter); raw `Vk*` handles in every signature; every header starts with the fixed comment block
(`Vulkan concept / Spec / Guide / Vulkan-Hpp / MoltenVK notes / Purpose / Ownership / See also`).
Lint: `grep -L "Vulkan concept" src/utils/vulkan/*.h` must print nothing. Spec anchors marked `verify`
have not been opened; check each once.

| Header | Vulkan concept | Notes |
|---|---|---|
| `Check.h` | `VkResult` error handling | `VK_CHECK(expr)` (throws `std::runtime_error` with file, line, symbolic name) + `vkResultString()`. Moved from `utils/VulkanCheck.h` (2026-09-29); the macro keeps its name. |
| `Init.h` | sType-tagged structs, swapchain results | `makeSubmitInfo()` etc. and `is_out_of_date()` / `swapchain_needs_recreation()`. Moved from `utils/VulkanInit.h`; namespace `swish::vk` became `swish::vkx`. |
| `Barrier.h` | Pipeline barriers, image layouts | constexpr `(old,new)` table (14 rows), pure `barrier_masks()`, `image_barrier()` (classic `VkImageMemoryBarrier`), `image_barrier2()` (sync2: `VkImageMemoryBarrier2` + `vkCmdPipelineBarrier2`, call only when `RendererServices::synchronization2`), `image_barrier_auto(cmd, useSync2, ...)`, `make_image_barrier2()` (pure struct fill), and `image_barrier_with()` / `kAfterTransferUndefToXferDst` for the one pair that needs non-table masks. Unsupported pair throws / `static_assert`s; no TOP/BOTTOM fallback. |
| `Rendering.h` | Dynamic rendering (Vulkan 1.3) | pure `color_attachment()` / `depth_attachment()` / `make_pipeline_rendering_info()` + the `RenderingInfo` builder (non-copyable, points into itself); `begin_rendering()` / `end_rendering()` are the only Vulkan calls and may only run when `RendererServices::dynamicRendering`. Tests: `tests/utils/test_vulkan_Rendering.cpp`. |
| `RenderScope.h` | Render pass instance, dynamic viewport/scissor | `class RenderScope` (RAII `vkCmdBeginRenderPass` / `vkCmdEndRenderPass`; was the renderer-private `ScopedRenderPass` struct) + `set_viewport_scissor()`. Users: `FullscreenPass`, `LightingPass`, `GeometryProducer`. |
| `RenderPass.h` | `VkRenderPass` (one colour, optional depth, one subpass) | `ColorPassDesc` / `AttachmentDesc` aggregates (designated initialisers), pure `RenderPassBuild` (fills the create info + the arrays it points to), `make_color_pass()` (the only Vulkan call), `subpass_dependency()`. 15 sites: Rain/Spray/Glass (via `renderer/Passes/ForwardPass.h`), Windshield x2, Velocity, DoF, TAA, the five `PostProcessManager` single-colour passes, IBL x2. The 4-MRT G-buffer and depth-only shadow passes keep their hand-written create info (different shape, one user each). Tests: `tests/utils/test_vulkan_RenderPass.cpp`. |
| `Descriptors.h` | Set layouts, pools, set allocation, sampler writes | `make_sampler_layout(dev, count or binding list, stages)`, `PoolSizes` aggregate + `make_pool()`, `allocate_set()` / `allocate_sets()`, `write_sampler()`; pure `sampler_layout_bindings()`, `pool_size_list()`, `sampler_write()`. Users: `PostProcessManager` (4 layouts, pool, 17 sets, writes), TAA, DoF, Rain, Spray x2, Windshield, `CameraUniforms`. Tests: `test_vulkan_Descriptors.cpp`. |
| `Samplers.h` | `VkSampler` | `SamplerDesc` aggregate + `make_sampler()`; deliberately no compare option (MoltenVK). Users: `PostProcessManager` x2 (linear + shadow), TAA, DoF, Windshield, IBL x3. Tests: `test_vulkan_Samplers.cpp`. |
| `ImageView.h` | `VkImageView` | `make_image_view2d()` (one mip, one layer, identity swizzle); pure `image_view2d_info()`. Users: `PostProcessManager` x3 (replaces its private `createImageView`), TAA, DoF, Velocity, Windshield x2. Tests: `test_vulkan_ImageView.cpp`. |
| `DebugLabel.h` | `VK_EXT_debug_utils` labels | `ScopedLabel`; real only under `SWISH_DEBUG_UI`, empty no-op otherwise (release identical). |
| `DeviceHandle.h` | Object lifetime | move-only `DeviceHandle<H, vkDestroyX, Services>`; holds a pointer to the services object, reads `services.device` at destroy, asserts it is alive. `Services` is any type with a `device` member (RendererServices fits structurally; `DeviceRef` is the default). |
| `ShaderModule.h` | Shader modules | `make_shader_module()` returning `UniqueShaderModule`. |
| `PushConstants.h` | Push constants | `push_range<T>()` static-asserts the 16-byte rule; `check_push_limit<T>(maxPushConstantsSize)`. |

## Lifetime rule (DeviceHandle)
The services object must outlive every handle and the `VkDevice` must still exist: handles die
before `Device::cleanup()` (same as `docs/vma-memory.md`).

## Adding a helper
1. New header here with the comment block; include only `<vulkan/vulkan.h>`, std, and a same-directory `"Check.h"`. Add `tests/utils/test_vulkan_<Name>.cpp` (headers only, no loader link).
2. Prefer `inline`/`constexpr`; no `.cpp` unless needed (sources under `src/` are globbed into `swish`).
3. Gate debug-only behaviour with `#ifdef SWISH_DEBUG_UI` and keep the release path empty.
4. For a barrier row: add it to `detail::kTable` in `Barrier.h` and prove it against the legacy code.

## Barrier equivalence (checked 2026-09-29 against `ResourceManager::insertImageBarrier`, now deleted)
8 pairs checked (6 via `insertImageBarrier` + 2 via `transitionImageLayout`): 6 identical, 2 differ on
purpose because the legacy helper silently fell through to TOP/BOTTOM_OF_PIPE with access 0. Both were migrated
in refactor R2 (luminance blit chain, now `Passes/HdrFinalizePass.cpp`, debug-UI only):

| Pair | Legacy masks | Now |
|---|---|---|
| `COLOR_ATTACHMENT -> TRANSFER_SRC` | `COLOR_OUTPUT/COLOR_WRITE -> BOTTOM_OF_PIPE/0` | `COLOR_OUTPUT/COLOR_WRITE -> TRANSFER/TRANSFER_READ` |
| `TRANSFER_SRC -> SHADER_READ` | `TOP_OF_PIPE/0 -> FRAGMENT/SHADER_READ` | `TRANSFER/TRANSFER_READ -> FRAGMENT/SHADER_READ` |

Rows added in R2 with masks copied from the local helpers they replaced (behaviour-identical, so release output is
unchanged): `SHADER_READ -> TRANSFER_DST`, `SHADER_READ -> TRANSFER_SRC`, `TRANSFER_DST -> COLOR_ATTACHMENT`,
`TRANSFER_SRC -> COLOR_ATTACHMENT`, plus `TRANSFER_DST -> TRANSFER_SRC` (luminance mip chain, with a `SubresourceSpan`).
The chain's first transition (`UNDEFINED -> TRANSFER_DST` on all mips) keeps its old `TRANSFER -> TRANSFER` scope via
`image_barrier_with(..., kAfterTransferUndefToXferDst)` because the pyramid is a single image shared by all frames in flight
(the table row uses `TOP_OF_PIPE`, right for fresh or per-frame images). Validate on Arch/NVIDIA with sync validation.

## Migration order (state after refactor R2, 2026-09-29; symbols, not line numbers)
1. Barrier, **done**: every call site uses `vkx` (`DofPass`, `TaaPass`, `WindshieldRainPass`, `IBLManager`, and the `Passes/` files);
   the local helpers and the `mipBarrier` lambda are gone. `TaaPass` records through `image_barrier_auto` (sync2 when enabled);
   every other site still records classic barriers.
2. `ResourceManager::insertImageBarrier`: **deleted** (no callers). `ResourceManager::transitionImageLayout` (one-shot texture
   upload and IBL equirect) still exists and is the last legacy barrier function; it has its own inline masks.
3. Shader module, **done**: `Pipeline.cpp` and `DepthOnlyPipeline.cpp` use `vkx::make_shader_module`. Still to do: a `DeviceHandle` pilot on `DofPass`.
4. Dynamic rendering + sync2, **started**: `Device` enables both features when supported; `TaaPass` is migrated. Next candidates
   (each keeps its classic path behind `RendererServices`): `DofPass` (same shape as TAA), then the rest of the post chain.
5. Push blocks, **done**: every push range goes through `push_range<T>` (`PostProcessParams` padded 52 to 64 B for the 16-byte rule).
6. Labels: wrap pass recording in `ScopedLabel` (debug-UI builds only); load the pointers in `VulkanContext`.

## Follow-ups not created yet (design doc list)
`Pipelines.h`, `Sync.h`, `Commands.h`, `Limits.h`. `DeviceHandle` pilot on the passes (the new builders return raw handles to
keep this refactor behaviour-identical; wrapping them is the next RAII step). Sync2 / dynamic rendering (V5): TaaPass
done, the remaining passes are later gated steps (`RenderPass.h` would then become a `RenderingInfo` aggregate for them).
