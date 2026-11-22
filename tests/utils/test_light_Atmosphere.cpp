// utils/light/Atmosphere.h: the raymarched clear sky (lighting L2.1). shaders/common/atmosphere.glsl is the same
// model in GLSL; these cases pin the physics both must show. Ranges are loose on purpose: the model has no
// multiple scattering, so absolute levels sit a little under a measured sky.
#include "utils/light/Atmosphere.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace ul = swish::util::light;
using Catch::Approx;

namespace {
// Sun at azimuth 180 (toward -Z), `el` degrees up.
glm::vec3 sun(float elDeg) {
    const float el = elDeg * swish::util::units::kRadPerDeg;
    return {0.0f, std::sin(el), -std::cos(el)};
}
const glm::vec3 kZenith(0.0f, 1.0f, 0.0f);
const glm::vec3 kHorizonSun     = glm::normalize(glm::vec3(0.0f, 0.05f, -1.0f));
const glm::vec3 kHorizonAntiSun = glm::normalize(glm::vec3(0.0f, 0.05f, 1.0f));
}  // namespace

TEST_CASE("L2: direct sun at the ground is about 100,000 lux at noon and reddens as it sets",
          "[lighting][atmosphere][L2]") {
    const glm::vec3 noon = ul::atm_transmittance(sun(60.0f), 2.5f);
    const float     lux  = ul::luminance(noon) * ul::kSunToaLux;
    CHECK(lux > 90000.0f);
    CHECK(lux < 125000.0f);
    CHECK(noon.r > noon.b);  // even at noon blue is scattered out a little

    const glm::vec3 low = ul::atm_transmittance(sun(2.0f), 2.5f);
    CHECK(ul::luminance(low) < 0.25f * ul::luminance(noon));            // far dimmer near the horizon
    CHECK(low.r > 5.0f * low.b);                                        // and red
    CHECK(ul::atm_transmittance(sun(-2.0f), 2.5f) == glm::vec3(0.0f));  // below the horizon: no direct sun
}

TEST_CASE("L2: the noon sky is blue overhead and brighter at the horizon", "[lighting][atmosphere][L2]") {
    const glm::vec3 zenith = ul::atm_scatter(kZenith, sun(60.0f), 2.5f) * ul::kSunToaLux;
    CHECK(ul::luminance(zenith) > 800.0f);  // a clear zenith is a few thousand nits
    CHECK(ul::luminance(zenith) < 8000.0f);
    CHECK(zenith.b > 2.0f * zenith.r);
    const glm::vec3 horizon = ul::atm_scatter(kHorizonAntiSun, sun(60.0f), 2.5f) * ul::kSunToaLux;
    CHECK(ul::luminance(horizon) > 2.0f * ul::luminance(zenith));
}

TEST_CASE("L2: sunset is red toward the sun and twilight fades as the sun sinks", "[lighting][atmosphere][L2]") {
    const glm::vec3 glow = ul::atm_scatter(kHorizonSun, sun(2.0f), 2.5f);
    CHECK(glow.r > 3.0f * glow.b);
    const auto zenithY = [](float el) {
        return ul::luminance(ul::atm_scatter(kZenith, sun(el), 2.5f)) * ul::kSunToaLux;
    };
    CHECK(zenithY(6.0f) > zenithY(0.0f));
    CHECK(zenithY(0.0f) > zenithY(-4.0f));
    CHECK(zenithY(-4.0f) > zenithY(-6.0f));
    CHECK(zenithY(-6.0f) > 0.05f);  // civil twilight: about a nit overhead
    CHECK(zenithY(-6.0f) < 10.0f);
    CHECK(zenithY(-40.0f) == Approx(0.0f).margin(1.0e-4));  // night: the planet shadows all the air
}

TEST_CASE("L2: more haze brightens the sky around the sun", "[lighting][atmosphere][L2]") {
    const glm::vec3 nearSun = glm::normalize(glm::vec3(0.0f, std::sin(0.6f), -std::cos(0.6f)));  // about 25 deg off
    CHECK(ul::luminance(ul::atm_scatter(nearSun, sun(60.0f), 8.0f)) >
          ul::luminance(ul::atm_scatter(nearSun, sun(60.0f), 2.0f)));
    CHECK(ul::atm_mie(2.5f) == Approx(1.0f));
}

TEST_CASE("L2: the whole sky: cloud cover, airglow, moon and the ground stand-in", "[lighting][atmosphere][L2]") {
    ul::SkyParams s;
    s.sunDir = sun(60.0f);
    ul::finish_sky_params(s);
    CHECK(s.overcastZenithNits == Approx(ul::luminance(ul::sky_radiance(s, kZenith))).epsilon(0.01));

    ul::SkyParams cloudy = s;
    cloudy.cloud         = 1.0f;
    const glm::vec3 c    = ul::sky_radiance(cloudy, kZenith);
    CHECK(c.r == Approx(c.b).epsilon(0.01));                                                // grey
    CHECK(ul::luminance(c) > 2.5f * ul::luminance(ul::sky_radiance(cloudy, kHorizonSun)));  // CIE: zenith 3x horizon

    // Below the horizon is a dimmed copy of the horizon, never black and never brighter than it.
    const float horizon = ul::luminance(ul::sky_radiance(s, glm::vec3(0.0f, 0.0f, 1.0f)));
    const float below   = ul::luminance(ul::sky_radiance(s, glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f))));
    CHECK(below == Approx(0.25f * horizon).epsilon(0.02));

    // Midnight: sun far below, a full moon (about 0.2 lux at the ground) and airglow.
    ul::SkyParams night;
    night.sunDir     = sun(-40.0f);
    night.moonDir    = glm::normalize(glm::vec3(0.3f, 0.7f, 0.2f));
    night.moonToaLux = 0.25f;
    ul::finish_sky_params(night);
    const float nightY = ul::luminance(ul::sky_radiance(night, kZenith));
    CHECK(nightY > ul::kAirglowNits);
    CHECK(nightY < 0.05f);
    // The cube scale keeps the baked zenith near 0.1 at noon and at midnight alike.
    CHECK(ul::luminance(ul::sky_radiance(s, kZenith)) / ul::sky_cube_scale(s) == Approx(0.1f).epsilon(0.01));
    CHECK(nightY / ul::sky_cube_scale(night) == Approx(0.1f).epsilon(0.01));
}
