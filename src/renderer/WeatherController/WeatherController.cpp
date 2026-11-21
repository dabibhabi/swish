#include "WeatherController.h"

#include "debug/DebugParams.h"
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/IBLManager/IBLManager.h"
#include "utils/io/AppPaths.h"
#include "utils/light/Photometry.h"
#ifdef SWISH_DEBUG_UI
#include "renderer/IBLManager/HdriCatalog.h"
#include "scene/TimeOfDay/TimeOfDay.h"
#include "utils/units/Units.h"
#endif

#include <glm/glm.hpp>
#include <string>

namespace swish {

namespace {

// One old scene unit (sun = 1.0, the scale every pass was tuned in) is 100,000 lux. Each preset keeps its
// previous look, so all carry the same sun illuminance and sky scale until the L2 time-of-day presets
// calibrate them (overcast has no direct sun, golden hour about 400 lux, and so on).
constexpr float kSunLux  = util::light::kLegacyUnitLux;
constexpr float kSkyNits = util::light::kLegacyUnitLux;

struct Look {
    Vec3  sunDir;
    Vec3  sunColor;  // tint only; the amount is sunLux
    float sunLux;    // illuminance on a surface facing the sun, lux
    float skyNits;   // scale applied to the baked sky cubes, cd/m²
    float clarity;
    float ambient;
    float hdriExposure;
    float hdriYaw;
    float evMin;  // auto-exposure EV100 clamp for this look (shipped EV100 is 15.7)
    float evMax;
};

Look look_for(WeatherPreset preset) {
    switch (preset) {
        case WeatherPreset::PartlyCloudy:
            return {glm::normalize(Vec3(0.30f, 0.78f, 0.22f)),
                    Vec3(1.00f, 0.97f, 0.90f),
                    kSunLux,
                    kSkyNits,
                    0.72f,
                    0.42f,
                    0.85f,
                    0.0f,
                    13.5f,
                    17.5f};
        case WeatherPreset::Cloudy:
            return {glm::normalize(Vec3(0.25f, 0.65f, 0.18f)),
                    Vec3(0.95f, 0.96f, 0.98f),
                    kSunLux,
                    kSkyNits,
                    0.35f,
                    0.48f,
                    0.90f,
                    0.0f,
                    13.0f,
                    17.0f};
        case WeatherPreset::VeryCloudy:
            return {glm::normalize(Vec3(0.20f, 0.55f, 0.15f)),
                    Vec3(0.90f, 0.92f, 0.95f),
                    kSunLux,
                    kSkyNits,
                    0.10f,
                    0.52f,
                    1.00f,
                    0.0f,
                    12.5f,
                    17.0f};
        case WeatherPreset::GoldenHour:
            return {glm::normalize(Vec3(0.85f, 0.28f, 0.20f)),
                    Vec3(1.00f, 0.72f, 0.42f),
                    kSunLux,
                    kSkyNits,
                    0.55f,
                    0.40f,
                    0.75f,
                    0.6f,
                    12.0f,
                    16.5f};
        default:
            return {glm::normalize(Vec3(0.30f, 0.78f, 0.22f)),
                    Vec3(1.00f, 0.97f, 0.90f),
                    kSunLux,
                    kSkyNits,
                    0.72f,
                    0.42f,
                    0.85f,
                    0.0f,
                    13.5f,
                    17.5f};
    }
}

}  // namespace

void WeatherController::set_preset(WeatherPreset preset) {
    m_preset = preset;
    if (!m_cameraUniforms) return;

    const Look look = look_for(preset);

    m_sunDir   = look.sunDir;
    m_sunColor = look.sunColor;
    m_sunLux   = look.sunLux;
    m_skyNits  = look.skyNits;
    m_clarity  = look.clarity;
    m_evMin    = look.evMin;
    m_evMax    = look.evMax;
    m_cameraUniforms->set_weather(Vec4(m_sunDir, m_sunLux), Vec4(m_sunColor, look.ambient), m_clarity, m_skyNits,
                                  util::light::kPreExposure);

#ifdef SWISH_DEBUG_UI
    // Lighting L2 (debug first): with the procedural sky on, the weather preset is cloud cover only. The time of
    // day owns the sun, the sky and the exposure clamps, and no HDRI is loaded (apply_debug bakes the sky).
    if (m_debug && m_debug->skyProcedural) {
        m_debug->cloudCover = preset == WeatherPreset::Cloudy       ? 0.6f
                              : preset == WeatherPreset::VeryCloudy ? 0.9f
                              : preset == WeatherPreset::GoldenHour ? 0.0f
                                                                    : 0.3f;
        m_debug->clarity    = m_clarity;
        return;
    }
#endif

    if (m_ibl) {
        const std::string_view file = weather_preset_hdri_file(preset);
        const std::string      path = util::paths::asset_dir() + "Sky/" + std::string(file);
#ifdef SWISH_DEBUG_UI
        const int hdriIndex = hdri_option_index(file);
#endif
        if (!m_ibl->set_hdri(path, look.hdriExposure, look.hdriYaw)) {
            // Missing asset -> procedural bake with the sun/clarity we just set.
            m_bakedSunDir   = Vec3(1e9f);
            m_bakedClarity  = -1.0f;
            m_bakedSunColor = Vec3(-1.0f);
            maybe_rebake_ibl();
        } else {
#ifdef SWISH_DEBUG_UI
            m_activeHdriIndex = hdriIndex;
#endif
            m_bakedSunDir   = m_sunDir;
            m_bakedClarity  = m_clarity;
            m_bakedSunColor = m_sunColor;
        }
    }

#ifdef SWISH_DEBUG_UI
    if (m_debug) {
        m_debug->clarity          = m_clarity;
        m_debug->sunColor         = m_sunColor;
        m_debug->evMin            = m_evMin;   // the panel shows, and may override, the preset's clamps
        m_debug->sunLux           = m_sunLux;  // likewise the sun and sky amounts (lab sliders)
        m_debug->skyNits          = m_skyNits;
        m_debug->evMax            = m_evMax;
        const int presetHdriIndex = hdri_option_index(weather_preset_hdri_file(preset));
        if (presetHdriIndex >= 0) {
            m_activeHdriIndex        = presetHdriIndex;
            m_debug->environmentHdri = presetHdriIndex;
        }
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
void WeatherController::load_time_of_day_presets() {
    m_todLoaded = true;
    toml::table tbl;
    try {
        tbl = toml::parse_file(util::paths::config_dir() + "time_of_day.toml");
    } catch (const toml::parse_error&) {
        // A missing or broken file keeps the built-in defaults (golden hour for every slot).
    }
    for (size_t i = 0; i < m_tod.size(); ++i) m_tod[i] = time_of_day_from_table(tbl, static_cast<TimeOfDay>(i));
}

// The procedural sky (lighting L2). The time-of-day preset is copied into the debug params once, when it changes,
// so every value stays a live slider afterwards. Each frame: pick the directional light (the sun, or the moon when
// the sun is down), tint it by the atmosphere, and rebake the sky cubes when anything the sky depends on moved.
void WeatherController::apply_procedural_sky(const DebugParams& d) {
    namespace ul = util::light;
    if (!m_todLoaded) load_time_of_day_presets();

    const int tod = glm::clamp(d.timeOfDay, 0, static_cast<int>(TimeOfDay::Count) - 1);
    if (tod != m_appliedTod) {
        const TimeOfDayPreset& p = m_tod[static_cast<size_t>(tod)];
        m_debug->sunAzimuth      = p.sunAzimuthDeg * util::units::kRadPerDeg;
        m_debug->sunElevation    = p.sunElevationDeg * util::units::kRadPerDeg;
        m_debug->sunLux          = p.sunLux;
        m_debug->moonLux         = p.moonLux;
        m_debug->turbidity       = p.turbidity;
        m_debug->evMin           = p.ev100Min;
        m_debug->evMax           = p.ev100Max;
        m_debug->lampsOn         = p.lampsOn;
        m_debug->sunAmbient      = 1.0f;  // the sky is physical now: no artistic gain on its diffuse light
        m_debug->autoExposure    = true;
        m_appliedTod             = tod;
    }
    const DebugParams& q = *m_debug;  // re-read: the block above may have just changed it

    // The true sun (may be below the horizon) and a moon standing opposite it, 45 degrees up.
    Vec3 sunDir = sun_dir_from_az_el(q.sunAzimuth * util::units::kDegPerRad, q.sunElevation * util::units::kDegPerRad);
    if (q.showSunGizmo) {
        static const Vec3 kBaseSunDir = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
        const Vec3        g           = glm::mat3(q.sunGizmoRot) * kBaseSunDir;
        if (glm::length(g) > 1e-4f) sunDir = glm::normalize(g);
    }
    const Vec3 moonDir = sun_dir_from_az_el(q.sunAzimuth * util::units::kDegPerRad + 180.0f, 45.0f);

    // Cloud cover: thin cloud leaves the sun alone; thick cloud removes almost all of the direct light.
    const float cover      = glm::clamp(q.cloudCover, 0.0f, 1.0f);
    const float sunThrough = 1.0f - 0.95f * ul::smoothstep01(0.3f, 1.0f, cover);
    const Vec3  sunT       = ul::atm_transmittance(sunDir, q.turbidity);
    const float sunDirect  = sunDir.y > 0.0f && ul::luminance(sunT) > 1.0e-4f ? q.sunLux * sunThrough : 0.0f;
    const float moonDirect = q.moonLux * sunThrough;

    if (sunDirect >= moonDirect && sunDirect > 0.0f) {
        m_sunDir   = sunDir;
        m_sunLux   = sunDirect;
        m_sunColor = sunT / std::max(std::max(sunT.r, sunT.g), std::max(sunT.b, 1.0e-6f));  // the tint only
    } else if (moonDirect > 0.0f) {
        m_sunDir   = moonDir;
        m_sunLux   = moonDirect;
        m_sunColor = Vec3(0.78f, 0.86f, 1.0f);  // cool moonlight
    } else {
        m_sunDir   = Vec3(0.0f, 1.0f, 0.0f);  // twilight: no directional light, the sky does the lighting
        m_sunLux   = 0.0f;
        m_sunColor = Vec3(1.0f);
    }

    ul::SkyParams sky;
    sky.sunDir    = sunDir;
    sky.sunToaLux = ul::kSunToaLux;
    sky.moonDir   = moonDir;
    // The moon's lux is given at the ground; undo its own transmittance to get the top-of-atmosphere amount.
    sky.moonToaLux  = q.moonLux > 0.0f
                          ? q.moonLux / std::max(ul::luminance(ul::atm_transmittance(moonDir, q.turbidity)), 0.05f)
                          : 0.0f;
    sky.turbidity   = q.turbidity;
    sky.cloud       = cover * cover;  // partly cloudy barely greys the sky; overcast is fully grey
    sky.airglowNits = ul::kAirglowNits;
    ul::finish_sky_params(sky);
    m_skyNits         = ul::sky_cube_scale(sky);
    m_debug->sky      = sky;
    m_debug->sunColor = m_sunColor;  // shown in the panel
    m_debug->skyNits  = m_skyNits;

    m_clarity = q.clarity;
    m_evMin   = q.evMin;
    m_evMax   = q.evMax;
    m_cameraUniforms->set_weather(Vec4(m_sunDir, m_sunLux), Vec4(m_sunColor, q.sunAmbient), m_clarity, m_skyNits,
                                  ul::kPreExposure);

    const auto same = [](const ul::SkyParams& a, const ul::SkyParams& b) {
        return a.sunDir == b.sunDir && a.moonDir == b.moonDir && a.moonToaLux == b.moonToaLux &&
               a.turbidity == b.turbidity && a.cloud == b.cloud;
    };
    if (m_ibl && (!m_skyBaked || !same(sky, m_bakedSky))) {
        m_ibl->bake_atmosphere(sky, m_skyNits);
        m_bakedSky        = sky;
        m_skyBaked        = true;  // cleared again when an HDRI replaces the cubes (apply_debug)
        m_activeHdriIndex = -1;    // the HDRI is gone from the cubes
    }
}

void WeatherController::apply_debug(const DebugParams& d, float wetness) {
    // Rain round 3 (owner decision): any rain uses an overcast HDRI, even when the procedural sky is ticked. The
    // latch holds it after the rain stops until the road dries, so sky and road never disagree.
    if (d.rainIntensity > 0.0f)
        m_debug->rainSkyActive = true;
    else if (wetness < d.rainSkyClearWetness)
        m_debug->rainSkyActive = false;
    const bool rainSky = m_debug->rainSkyActive;
    // L2.4: keep the rain HDRI decoded AND uploaded in the background while it is dry (before the procedural early
    // return), so the onset swap is only the ~5 ms bake.
    if (m_ibl && d.rainEnvironmentHdri != m_prefetchedRainHdri) {
        const int i = glm::clamp(d.rainEnvironmentHdri, 0, static_cast<int>(kHdriOptions.size()) - 1);
        m_ibl->prefetch_hdri(util::paths::asset_dir() + "Sky/" + std::string(kHdriOptions[i].file));
        m_prefetchedRainHdri = d.rainEnvironmentHdri;
    }
    if (m_ibl) m_ibl->poll_prefetch();
    {
        const auto  now = std::chrono::steady_clock::now();
        const float dt  = m_lastApply.time_since_epoch().count() == 0
                              ? 0.0f
                              : std::min(std::chrono::duration<float>(now - m_lastApply).count(), 0.1f);
        m_lastApply     = now;
        // Fade-in only: the background blends procedural -> HDRI while the rain sky is on. It snaps back to 0 when
        // the latch clears (the cubes are procedural again by then). Unticked procedural sky: nothing to fade from.
        if (!rainSky || !d.skyProcedural)
            m_debug->rainSkyBlend = rainSky ? 1.0f : 0.0f;
        else
            m_debug->rainSkyBlend =
                std::min(1.0f, m_debug->rainSkyBlend + dt / std::max(d.rainSkyFadeSeconds, 1.0e-3f));
    }
    if (procedural_sky_active(d)) {
        apply_procedural_sky(d);
        m_wasProcedural = true;
        return;
    }
    if (m_wasProcedural) {
        // Back to the HDRI weather presets: restore the legacy scale and force the HDRI to reload.
        m_wasProcedural   = false;
        m_appliedTod      = -1;
        m_skyBaked        = false;
        m_activeHdriIndex = -1;
        m_debug->sunLux = m_debug->skyNits = util::light::kLegacyUnitLux;
        m_debug->sunAmbient                = 0.50f;
        const Look look                    = look_for(m_preset);
        m_debug->evMin                     = look.evMin;
        m_debug->evMax                     = look.evMax;
        m_debug->sunColor                  = look.sunColor;
    }
    // Sun-orientation gizmo: derive the sun direction from the gizmo's rotation
    // (applied to the shipped base direction) before it feeds set_weather below.
    if (d.showSunGizmo) {
        static const Vec3 kBaseSunDir = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
        Vec3              dir         = glm::mat3(d.sunGizmoRot) * kBaseSunDir;
        if (glm::length(dir) > 1e-4f) m_sunDir = glm::normalize(dir);
    } else {
        // No gizmo: the Azimuth / Elevation sliders (radians) own the direction (L0.7).
        m_sunDir = sun_dir_from_az_el(d.sunAzimuth * util::units::kDegPerRad, d.sunElevation * util::units::kDegPerRad);
    }
    // Sun colour/ambient/clarity feed the CameraUBO (written by update() right after).
    m_clarity  = d.clarity;
    m_sunColor = d.sunColor;
    m_evMin    = d.evMin;
    m_sunLux   = d.sunLux;
    m_skyNits  = d.skyNits;
    m_evMax    = d.evMax;
    if (rainSky) {
        // Overcast rain: no direct sun (no shadows, no sun glint, no god-ray source), no clear air (full rain fog,
        // more windshield blur), and a lower exposure clamp. The fog / gloss strength still scales with wetness.
        m_clarity = 0.0f;
        m_sunLux  = 0.0f;
        m_evMin -= d.rainEvShift;
        m_evMax -= d.rainEvShift;
    }
    m_cameraUniforms->set_weather(Vec4(m_sunDir, m_sunLux), Vec4(m_sunColor, d.sunAmbient), m_clarity, m_skyNits,
                                  util::light::kPreExposure);
    // While it rains the rain HDRI wins over both the panel choice and the weather preset's file.
    const int hdriIndex =
        glm::clamp(rainSky ? d.rainEnvironmentHdri : d.environmentHdri, 0, static_cast<int>(kHdriOptions.size()) - 1);
    if (m_ibl && hdriIndex != m_activeHdriIndex) {
        const HdriOption& option = kHdriOptions[hdriIndex];
        const std::string path   = util::paths::asset_dir() + "Sky/" + std::string(option.file);
        if (m_ibl->set_hdri(path, option.exposure, option.yaw)) {
            m_activeHdriIndex = hdriIndex;
            m_skyBaked        = false;  // the cubes now hold the HDRI: the procedural sky must rebake when it returns
        }
    }
    // Re-bake the sky cubemaps if the weather changed (dirty-checked; only on change).
    maybe_rebake_ibl();
}
#endif

}  // namespace swish
