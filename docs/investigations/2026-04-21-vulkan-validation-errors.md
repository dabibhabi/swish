# 2026-04-21 — Vulkan validation errors on Linux

## Background

The project's primary target is Arch Linux + NVIDIA. Until commit
`eb442ae` the `CMakeLists.txt` had hard `find_library(Cocoa REQUIRED)` /
`IOKit` / `OpenGL` calls that blocked Linux configure, so all prior
builds had been on macOS where MoltenVK's validation path is more
permissive. Enabling the Linux build surfaced two classes of errors
the macOS path had been hiding.

## Symptom

On Arch Linux, GCC 15.2.1, Vulkan 1.4.341, two families of
validation-layer messages appeared:

1. `VUID-vkQueueSubmit-pSignalSemaphores-00067` — once per frame-in-flight
   slot, then throttled by the validator's duplicate-message limit.
   > `pSubmits[0].pSignalSemaphores[0] (VkSemaphore …) is being signaled
   > by VkQueue …, but it may still be in use by VkSwapchainKHR …`
2. `VUID-vkCmdDraw-None-09600` — a sampled image was bound in
   `VK_IMAGE_LAYOUT_UNDEFINED` when the shader expected
   `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.





## Root cause A — `renderFinished` semaphores were sized by frame-in-flight, not swapchain image

`SyncObjects` keyed its `renderFinished` semaphore pool by frame-in-flight
slot (size `MAX_FRAMES_IN_FLIGHT = 2`). With MAILBOX present mode the
swapchain has 3 images. A binary semaphore signaled by `vkQueueSubmit`
and waited on by `vkQueuePresentKHR` cannot be re-signaled until the
presentation engine has consumed it, and MAILBOX can hold an image
across more than one CPU frame — so slot N comes back around to submit
before swap image M's prior present has released the semaphore.

The existing band-aid (`SyncObjects::recreateRenderFinishedSemaphore`,
called only on `OUT_OF_DATE` / `SUBOPTIMAL` / resize) never fires in
steady state, so the validator fired every frame.

### Fix applied

Switched to per-image semaphore indexing — option (a) from the Vulkan
swapchain-semaphore-reuse guide at
<https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html>.

- `SyncObjects::init` now takes `swapchainImageCount` and sizes the
  `renderFinished` pool to match.
- `SyncObjects::recreateRenderFinishedSemaphores` (plural) rebuilds the
  pool on swapchain recreate. Full destroy+create, not just a resize:
  the Vulkan spec permits binary semaphores to remain signaled when
  `vkQueuePresentKHR` returns an error, so recreate is what guarantees
  unsignaled state.
- `vkQueueSubmit` / `vkQueuePresentKHR` in `Renderer::drawFrame` now
  signal and wait on `getRenderFinished(imageIndex)` rather than
  `(m_currentFrame)`.
- `Renderer::recreateSwapchain` is now the single owner of swapchain-
  lifecycle sync fixups. `drawFrame` no longer carries a special case.

`imageAvailable` stays keyed by frame-in-flight slot because
`vkAcquireNextImageKHR` signals it *before* `imageIndex` is known. The
per-frame fence also stays keyed by slot — it guards CPU command-buffer
reuse, not the swapchain.

Files touched: `src/renderer/SyncObjects/SyncObjects.{h,cpp}`,
`src/renderer/Renderer/Renderer.cpp`.

## Root cause B — dormant SSAO image is still bound in the composite descriptor

`PostProcessManager` allocates `m_aoImage` / `m_aoBlurImage` for the SSAO
pass, but SSAO is currently disabled (see the
`(SSAO disabled for initial bring-up — AO texture is white/1.0)` comment
in `Renderer::recordCommandBuffer`). The composite pass still writes
`m_aoBlurView` into its descriptor set at
`PostProcessManager.cpp:545`. Because no SSAO render pass ever runs,
the image's layout never transitions out of `UNDEFINED`, and the
composite shader's sample against it trips VUID-09600.

### Fix

*Not yet implemented.* Planned approach: rebind the composite
descriptor's AO slot to a 1×1 white fallback texture while SSAO is off.
This matches the intent of the bring-up comment (AO treated as 1.0)
and removes the unused-but-bound resource at the source rather than
just priming its layout. A larger-scope alternative is to actually
wire SSAO into the per-frame record path.
