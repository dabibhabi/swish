Searched for regex `rain|GLFW_KEY_R|set_rain_intensity|m_rainIntensity` (`**/src/core/App/**`), 24 results

Here's the full call chain from key press to pixels on screen, based on App.cpp and Renderer.cpp:

```mermaid
graph TD
    K["R key press<br/>(App::processInput)"] --> RL["m_rain_level cycles<br/>0→1→2 (off/light/heavy)"]
    RL --> SRI["Renderer::set_rain_intensity(rain)"]
    RL --> CRI["CarEntity::set_rain_intensity(rain)<br/>(cabin wash tint)"]
    SRI --> MRI["m_rainIntensity stored on Renderer"]

    subgraph "Every frame — Renderer::drawFrame()"
        MRI --> UP["RainSystem::update(frameIndex, dt, m_rainIntensity, effectiveWind)"]
        UP --> WIND["gust wind = base wind ⊕ 2 sinusoids − m_carVelocity"]
        UP --> WET["m_wetness accumulates/decays toward m_intensity"]
        UP --> UBO["writes RainUBO (near) + RainUBO far-layer<br/>into per-frame mapped buffers"]
        UP --> GW["get_wetness() → CameraUniforms::set_wetness(frameIndex, wetness)"]

        UBO --> RCB["Renderer::recordCommandBuffer"]
        RCB --> GB["recordGBufferPass (writes scene depth)"]
        GB --> TR["transitionGBufferForLighting (depth → READ_ONLY)"]
        TR --> LP["recordLightingPass"]
        LP --> SSR["SSR / god-rays passes"]
        SSR --> RP["Renderer::recordRainPass(cmd, frameIndex)"]
    end

    RP --> BIND["bind camera set 0 via rain pipeline layout"]
    BIND --> RD["RainSystem::record_draws(cmd, frameIndex)"]
    RD --> CHK["early-out if m_intensity < 0.001"]
    RD --> BP["bind pipeline, quad VBO + instance buffer, quad IBO"]
    RD --> DC["compute drawCount ∝ intensity"]
    RD --> FAR["bind m_descSetsFar → vkCmdDrawIndexed (far layer)"]
    RD --> NEAR["bind m_descSets → vkCmdDrawIndexed (near layer)"]

    NEAR --> VS["rain.vert: per-instance seed → wrapped world pos,<br/>Marshall-Palmer diameter, Gunn-Kinzer speed, billboard build"]
    VS --> FS["rain.frag: Garg-Nayar streak shading, additive blend onto HDR"]
```

Key points:
- **Input → state**: `App::processInput` detects `GLFW_KEY_R`, cycles `m_rain_level` through `{0.0, 0.35, 1.0}`, and pushes it via `Renderer::set_rain_intensity()` (plus mirrors it to `CarEntity::set_rain_intensity()` for the cabin-wash tint).
- **Per-frame simulation**: `Renderer::drawFrame()` calls `RainSystem::update()` unconditionally every frame (even at intensity 0) — it advances gust wind, wetness, and writes both near/far `RainUBO`s.
- **Per-frame draw**: `Renderer::recordRainPass()` binds the camera descriptor set for the rain pipeline layout, then delegates to `RainSystem::record_draws()`, which does an early return if intensity is negligible, otherwise draws the far parallax layer then the near layer (same geometry/instance buffer, different descriptor set/UBO).
- **GPU side**: all the drop motion, size (Marshall–Palmer), and fall speed (Gunn–Kinzer) happen in rain.vert; the streak shape/shimmer and additive output happen in rain.frag — no CPU-side per-drop work at all.