#include "utils/io/AppPaths.h"
#include "WeatherController.h"

#include "debug/DebugParams.h"
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/IBLManager/IBLManager.h"

#include <glm/glm.hpp>
#include <string>

namespace swish {

namespace {

struct Look {
    Vec3  sunDir;
    Vec3  sunColor;
    float clarity;
    float ambient;
    float hdriExposure;
    float hdriYaw;
};

Look look_for(WeatherPreset preset) {
    switch (preset) {
        case WeatherPreset::PartlyCloudy:
            return {glm::normalize(Vec3(0.30f, 0.78f, 0.22f)), Vec3(1.00f, 0.97f, 0.90f), 0.72f, 0.42f, 0.85f, 0.0f};
        case WeatherPreset::Cloudy:
            return {glm::normalize(Vec3(0.25f, 0.65f, 0.18f)), Vec3(0.95f, 0.96f, 0.98f), 0.35f, 0.48f, 0.90f, 0.0f};
        case WeatherPreset::VeryCloudy:
            return {glm::normalize(Vec3(0.20f, 0.55f, 0.15f)), Vec3(0.90f, 0.92f, 0.95f), 0.10f, 0.52f, 1.00f, 0.0f};
        case WeatherPreset::GoldenHour:
            return {glm::normalize(Vec3(0.85f, 0.28f, 0.20f)), Vec3(1.00f, 0.72f, 0.42f), 0.55f, 0.40f, 0.75f, 0.6f};
        default:
            return {glm::normalize(Vec3(0.30f, 0.78f, 0.22f)), Vec3(1.00f, 0.97f, 0.90f), 0.72f, 0.42f, 0.85f, 0.0f};
    }
}

}  // namespace

void WeatherController::set_preset(WeatherPreset preset) {
    m_preset = preset;
    if (!m_cameraUniforms) return;

    const Look look = look_for(preset);

    m_sunDir   = look.sunDir;
    m_sunColor = look.sunColor;
    m_clarity  = look.clarity;
    m_cameraUniforms->set_weather(Vec4(m_sunDir, 1.0f), Vec4(m_sunColor, look.ambient), m_clarity);

    if (m_ibl) {
        const std::string path = util::paths::asset_dir() + "Sky/" + std::string(weather_preset_hdri_file(preset));
        if (!m_ibl->set_hdri(path, look.hdriExposure, look.hdriYaw)) {
            // Missing asset -> procedural bake with the sun/clarity we just set.
            m_bakedSunDir   = Vec3(1e9f);
            m_bakedClarity  = -1.0f;
            m_bakedSunColor = Vec3(-1.0f);
            maybe_rebake_ibl();
        } else {
            m_bakedSunDir   = m_sunDir;
            m_bakedClarity  = m_clarity;
            m_bakedSunColor = m_sunColor;
        }
    }

#ifdef SWISH_DEBUG_UI
    if (m_debug) {
        m_debug->clarity  = m_clarity;
        m_debug->sunColor = m_sunColor;
    }
#endif
}

void WeatherController::maybe_rebake_ibl() {
    if (!m_ibl) return;
    if (m_ibl->uses_hdri()) return;
    if (m_sunDir == m_bakedSunDir && m_clarity == m_bakedClarity && m_sunColor == m_bakedSunColor) return;

#ifdef SWISH_DEBUG_UI
    const DebugParams& d = *m_debug;
#else
    const DebugParams d{};
#endif
    IBLManager::SkyBakeParams sky;
    sky.horizon  = glm::mix(d.skyHorizonOvercast, d.skyHorizonClear, m_clarity);
    sky.zenith   = glm::mix(d.skyZenithOvercast, d.skyZenithClear, m_clarity);
    sky.discExp  = glm::mix(d.sunDiscExpMin, d.sunDiscExpMax, m_clarity);
    sky.discStr  = glm::mix(d.sunDiscStrMin, d.sunDiscStrMax, m_clarity);
    sky.sunDir   = m_sunDir;
    sky.sunColor = m_sunColor;
    m_ibl->bake(sky);

    m_bakedSunDir   = m_sunDir;
    m_bakedClarity  = m_clarity;
    m_bakedSunColor = m_sunColor;
}

#ifdef SWISH_DEBUG_UI
void WeatherController::apply_debug(const DebugParams& d) {
    // Sun-orientation gizmo: derive the sun direction from the gizmo's rotation
    // (applied to the shipped base direction) before it feeds set_weather below.
    if (d.showSunGizmo) {
        static const Vec3 kBaseSunDir = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
        Vec3              dir         = glm::mat3(d.sunGizmoRot) * kBaseSunDir;
        if (glm::length(dir) > 1e-4f) m_sunDir = glm::normalize(dir);
    }
    // Sun colour/ambient/clarity feed the CameraUBO (written by update() right after).
    // Direction stays m_sunDir for now (azimuth/elevation wiring is a later phase).
    m_clarity  = d.clarity;
    m_sunColor = d.sunColor;
    m_cameraUniforms->set_weather(Vec4(m_sunDir, 1.0f), Vec4(m_sunColor, d.sunAmbient), m_clarity);
    // Re-bake the sky cubemaps if the weather changed (dirty-checked; only on change).
    maybe_rebake_ibl();
}
#endif

}  // namespace swish
