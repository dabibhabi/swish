#pragma once
// Debug-only pipeline rebuild entry points of registry passes, called by the shader hot-reload hooks (S5). A pass that
// can rebuild its pipeline from a freshly compiled .spv exposes one function here; the Renderer registers it with
// ShaderHotReload under the shader stem. Every call is vkDeviceWaitIdle-safe and must run between frames.
//
// TODO(stub): only Composite is wired. Others (ssr, godrays, bloom_*, ssao, ao_blur; lighting and the forward passes
// own their pipelines) log "TODO(stub)" and need a restart.

#ifdef SWISH_DEBUG_UI

namespace swish {

class PostProcessManager;

// composite.frag (+ fullscreen.vert): recreate the composite pipeline. Throws if the .spv cannot be loaded; the old
// pipeline then stays in use.
void rebuild_composite_pass(PostProcessManager& pm);

}  // namespace swish

#endif  // SWISH_DEBUG_UI
