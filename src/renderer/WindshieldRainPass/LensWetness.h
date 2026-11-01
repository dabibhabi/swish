#pragma once

// ══════════════════════════════════════════════════════════════════════
// LensWetness — CPU-only wet-lens model for lamp lenses (STUB).
//
// Purpose : per-lens {coverage, dropSize, streak}, keyed by glTF lens node name ("Headlight_L_Lens",
//           "TailLight_R_Lens", ...). No dependency on any lights code; the caller supplies names.
// Units   : speed in m/s, time in s, everything else unitless [0,1].
// Test    : tests/test_lens_wetness.cpp
//
// Model (per lens, per step):
//   wetting  = rain * wetRate * (1 - coverage)                   accumulate with rain
//   airflow  = smoothstep(clearSpeedStart, clearSpeedFull, speed) smooth ramp, 0 below start, 1 above full
//   clearing = airflow * clearRate * coverage                    blown off by airflow
//   draining = drainRate * coverage                              slow gravity drain (also when parked / dry)
//   coverage += (wetting - clearing - draining) * dt, clamped [0,1]; exactly 0 when dry and fully drained.
//   dropSize = grows with coverage (beads merge); streak = coverage * airflow (drops smear along the flow).
//
// TODO(stub): what the full version must do:
//   - Per-lens orientation: lenses facing the airflow (headlights) clear faster than tail lights in the wake; take a
//     per-lens exposure factor in addLens().
//   - Drop merging / lifetime (stochastic bead population) rather than a scalar, and heat (a lit lamp dries its lens).
//   - Wiper-style hand-off is not needed, but windshield spray/splash hits from SpraySystem should wet rear lenses.
//   - Renderer side: upload LensWetnessGpu per lens into the lighting/emissive path (not wired yet).
//
// LensWetnessGpu is std140-friendly: a single vec4 (16 bytes), array stride 16.
// ══════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace swish {

struct LensWetnessParams {
    float wetRate         = 0.35f;  // 1/s coverage gain at rain = 1 on a dry lens
    float clearSpeedStart = 8.0f;   // m/s: airflow starts clearing (~18 mph)
    float clearSpeedFull  = 25.0f;  // m/s: airflow clears at full rate (~56 mph)
    float clearRate       = 3.0f;   // 1/s clearing at full airflow (steady state at full airflow in rain 1 ≈ 0.10)
    float drainRate       = 0.02f;  // 1/s slow drain
    float dropSizeMin     = 0.15f;  // dropSize at tiny coverage
    float dropSizeMax     = 1.0f;   // dropSize at full coverage
};

// Per-lens state (CPU).
struct LensWetnessState {
    float coverage = 0.0f;  // [0,1] fraction of the lens covered by water
    float dropSize = 0.0f;  // [0,1] normalised bead size (0 when dry)
    float streak   = 0.0f;  // [0,1] airflow smear amount
};

// GPU-uploadable POD: one vec4 per lens (16 bytes).
struct LensWetnessGpu {
    float coverage;
    float dropSize;
    float streak;
    float pad_;
};
static_assert(sizeof(LensWetnessGpu) == 16, "LensWetnessGpu must be a 16-byte multiple (std140)");

class LensWetnessModel {
public:
    explicit LensWetnessModel(const LensWetnessParams& p = {}) : m_p(p) {}

    // Idempotent: adds the lens (dry) if absent.
    void add_lens(const std::string& name) { m_lens.try_emplace(name); }

    // Advance every known lens. rainIntensity in [0,1].
    void step(float dt, float rainIntensity, float speedMps) {
        const float rain    = std::clamp(rainIntensity, 0.0f, 1.0f);
        const float airflow = airflow_factor(speedMps);
        const float d       = std::max(dt, 0.0f);
        for (auto& kv : m_lens) {
            LensWetnessState& s       = kv.second;
            const float       wetting = rain * m_p.wetRate * (1.0f - s.coverage);
            const float       clear   = airflow * m_p.clearRate * s.coverage;
            const float       drain   = m_p.drainRate * s.coverage;
            s.coverage                = std::clamp(s.coverage + (wetting - clear - drain) * d, 0.0f, 1.0f);
            if (s.coverage < 1e-4f && rain <= 0.0f) s.coverage = 0.0f;  // fully dry: no residual
            s.dropSize = s.coverage > 0.0f ? m_p.dropSizeMin + (m_p.dropSizeMax - m_p.dropSizeMin) * s.coverage : 0.0f;
            s.streak   = s.coverage * airflow;
        }
    }

    // Dry state for an unknown name.
    LensWetnessState get(const std::string& name) const {
        auto it = m_lens.find(name);
        return it == m_lens.end() ? LensWetnessState{} : it->second;
    }

    LensWetnessGpu gpu(const std::string& name) const {
        const LensWetnessState s = get(name);
        return {s.coverage, s.dropSize, s.streak, 0.0f};
    }

    void reset() {
        for (auto& kv : m_lens) kv.second = {};
    }

    const LensWetnessParams& params() const { return m_p; }
    // Live retune (debug sliders): keeps every lens's state, only changes the rates used by the next step().
    void set_params(const LensWetnessParams& p) { m_p = p; }

    // Smooth 0..1 airflow-clearing ramp.
    float airflow_factor(float speedMps) const {
        const float span = std::max(m_p.clearSpeedFull - m_p.clearSpeedStart, 1e-3f);
        const float t    = std::clamp((speedMps - m_p.clearSpeedStart) / span, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

private:
    LensWetnessParams                                 m_p;
    std::unordered_map<std::string, LensWetnessState> m_lens;
};

}  // namespace swish
