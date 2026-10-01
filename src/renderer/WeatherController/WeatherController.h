#pragma once

// WeatherController (R5): the sun / clarity / HDRI look of the driving weather and the IBL bake that follows it.
// Moved out of Renderer (set_weather_preset, maybeRebakeIBL and the weather half of apply_debug_params).
//
// Owns: the active preset, the sun direction / colour / clarity the frame uses (CSM fit, god-rays and the camera UBO
// all read sun_dir()), and the "what was last baked" state that keeps IBL rebakes dirty-checked.
// Borrows (non-owning, bound by Renderer): the CameraUniforms it writes the weather into, the IBLManager it bakes
// into, and (debug builds) the DebugParams whose clarity / sunColor mirror the preset.

#include "scene/WeatherPreset.h"
#include "utils/types/Types.h"

namespace swish {

class CameraUniforms;
class IBLManager;
struct DebugParams;

class WeatherController {
public:
    void bind_camera_uniforms(CameraUniforms* cu) { m_cameraUniforms = cu; }
    void bind_ibl(IBLManager* ibl) { m_ibl = ibl; }
#ifdef SWISH_DEBUG_UI
    void bind_debug_params(DebugParams* d) { m_debug = d; }
#endif

    // Apply a preset: sun + clarity into the camera UBO, HDRI bake from assets/Sky/ (procedural sky if the file is
    // missing). A no-op apart from remembering the preset until the CameraUniforms is bound.
    void          set_preset(WeatherPreset preset);
    WeatherPreset preset() const { return m_preset; }

    const Vec3& sun_dir() const { return m_sunDir; }

#ifdef SWISH_DEBUG_UI
    // Live debug edits (sun gizmo, clarity, sun colour, ambient) -> camera UBO, then a dirty-checked IBL rebake.
    void apply_debug(const DebugParams& d);
#endif

private:
    // (Re)bake procedural-sky IBL when the weather changed. An HDRI environment is baked by set_hdri (set_preset) and
    // must not be overwritten by this path.
    void maybe_rebake_ibl();

    CameraUniforms* m_cameraUniforms = nullptr;
    IBLManager*     m_ibl            = nullptr;
#ifdef SWISH_DEBUG_UI
    DebugParams* m_debug = nullptr;
#endif

    WeatherPreset m_preset = WeatherPreset::PartlyCloudy;

    Vec3  m_sunDir   = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
    float m_clarity  = 0.0f;
    Vec3  m_sunColor = Vec3(1.0f, 0.95f, 0.85f);

    Vec3  m_bakedSunDir   = Vec3(1e9f);
    float m_bakedClarity  = -1.0f;
    Vec3  m_bakedSunColor = Vec3(-1.0f);
};

}  // namespace swish
