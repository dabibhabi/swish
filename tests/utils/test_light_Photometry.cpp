// utils/light/Photometry.h, the learning scaffold for tasks/todo.md "Lighting" phases L1, L5 and L6.
// Hidden ([.learning]): `make test` skips them; run
// `build/tests/utils/swish_utils_tests "[learning][photometry]"`. They fail until the stubs are filled in.
// When a phase passes, drop `.learning` from its tags so ctest guards it.
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace ul = swish::util::light;
namespace uu = swish::util::units;
using Catch::Approx;

TEST_CASE("L1: exposure halves for every EV100 stop", "[lighting][photometry][L1]") {
    CHECK(ul::exposure_from_ev100(0.0f) == Approx(1.0f / 1.2f));
    CHECK(ul::exposure_from_ev100(15.0f) == Approx(1.0f / (1.2f * 32768.0f)));  // sunny noon
    CHECK(ul::exposure_from_ev100(4.0f) == Approx(0.5f * ul::exposure_from_ev100(3.0f)));
}

TEST_CASE("L1: EV100 from average luminance", "[lighting][photometry][L1]") {
    CHECK(ul::ev100_from_luminance(0.125f) == Approx(0.0f).margin(1e-4));
    CHECK(ul::ev100_from_luminance(4096.0f) == Approx(15.0f));
    CHECK(std::isfinite(ul::ev100_from_luminance(0.0f)));  // a black frame must not produce -inf
}

TEST_CASE("L1: metering then exposing lands every scene on the same grey", "[lighting][photometry][L1]") {
    // exposure(ev100(L)) * L = 1 / (1.2 * 8) whatever L is: that is what auto-exposure means.
    for (float lum : {0.05f, 3.0f, 250.0f, 8000.0f}) {
        const float exposed = ul::exposure_from_ev100(ul::ev100_from_luminance(lum)) * lum;
        CHECK(exposed == Approx(1.0f / 9.6f).epsilon(1e-3));
    }
}

TEST_CASE("L1: pre-exposure puts the old scene scale at 100,000 lux and keeps the shipped exposure",
          "[lighting][photometry][L1]") {
    // One old scene unit (sun = 1.0) is kLegacyUnitLux, and the pre-exposure is its inverse.
    CHECK(ul::kLegacyUnitLux * ul::kPreExposure == Approx(1.0f));
    // The composite multiplier is physical exposure / pre-exposure. 15.7144 is the EV100 that reproduces the
    // old fixed composite exposure of 1.55 (CompositePass.cpp kShippedEv100).
    CHECK(ul::hdr_exposure_from_ev100(15.7144f) == Approx(1.55f).epsilon(1e-3));
    // A white diffuse card under the noon sun, pre-exposed, lands on the old scale: rho * E / pi * P = 0.318.
    const float cardNits = 1.0f * 100000.0f / 3.14159265f;
    CHECK(cardNits * ul::kPreExposure == Approx(0.31831f).epsilon(1e-3));
}

TEST_CASE("L1: metering a pre-exposed HDR value recovers the true luminance", "[lighting][photometry][L1]") {
    // HdrFinalizePass divides the adapted HDR luminance by kPreExposure before metering.
    const float trueNits = 3183.0f;  // sunlit asphalt, albedo 0.1
    const float hdr      = trueNits * ul::kPreExposure;
    CHECK(ul::ev100_from_luminance(hdr / ul::kPreExposure) == Approx(14.64f).epsilon(1e-3));
    // And the composite multiplier lands it on the same grey as the unscaled path: 1 / 9.6.
    const float ev = ul::ev100_from_luminance(hdr / ul::kPreExposure);
    CHECK(ul::hdr_exposure_from_ev100(ev) * hdr == Approx(1.0f / 9.6f).epsilon(1e-3));
}

TEST_CASE("Lab: adaptive pre-exposure keeps the composite multiplier constant", "[lighting][photometry][lab]") {
    // At the shipped EV100 the adaptive pre-exposure is the fixed one.
    CHECK(ul::pre_exposure_for_ev100(15.7144f) == Approx(ul::kPreExposure).epsilon(1e-3));
    // Whatever the EV, exposure / pre-exposure is the shipped 1.55, so all adaptation lives in the pre-exposure.
    for (float ev : {-4.0f, 2.0f, 9.0f, 15.7144f, 17.0f}) {
        const float P = ul::pre_exposure_for_ev100(ev);
        CHECK(ul::hdr_exposure_from_ev100(ev, P) == Approx(ul::kShippedHdrExposure));
    }
    // A lamp-lit night road (0.5 cd/m2, about EV 2) lands at a comfortable half-float value, not near the floor.
    const float evNight = ul::ev100_from_luminance(0.5f);
    CHECK(0.5f * ul::pre_exposure_for_ev100(evNight) > 0.01f);
    CHECK(0.5f * ul::kPreExposure < 1.0e-5f);  // the fixed pre-exposure would put it here
}

TEST_CASE("L1: lumens to candela", "[lighting][photometry][L1]") {
    CHECK(ul::point_candela_from_lumens(1000.0f) == Approx(79.577f).epsilon(1e-4));  // 1000 / (4 pi)
    CHECK(ul::spot_candela_from_lumens(1000.0f) == Approx(318.31f).epsilon(1e-4));   // 1000 / pi
    CHECK(ul::point_candela_from_lumens(0.0f) == 0.0f);
}

TEST_CASE("L1: the cone rule equals the Frostbite rule at 60 degrees and grows as the beam narrows",
          "[lighting][photometry][L1]") {
    const float lm = 1200.0f, rad60 = uu::kPi / 3.0f;
    CHECK(ul::cone_candela_from_lumens(lm, rad60) == Approx(ul::spot_candela_from_lumens(lm)).epsilon(1e-4));
    CHECK(ul::cone_candela_from_lumens(lm, 28.0f * uu::kRadPerDeg) == Approx(1631.0f).epsilon(2e-3));
    CHECK(ul::cone_candela_from_lumens(lm, 7.0f * uu::kRadPerDeg) > 20000.0f);  // a tight hot spot
    CHECK(std::isfinite(ul::cone_candela_from_lumens(lm, 0.0f)));               // a zero angle must not divide by zero
}

TEST_CASE("L1: a street lamp on a highway pole lights the road to a few tens of lux", "[lighting][photometry][L1]") {
    // 16,000 lm sodium lamp, 30 ft pole (RoadScene_Furniture.cpp): E = I / h^2 directly below.
    const float I = ul::point_candela_from_lumens(16000.0f);
    CHECK(I == Approx(1273.2f).epsilon(1e-3));
    const float h = 30.0f * uu::kMetresPerFt;  // metres
    const float E = I * ul::inverse_square_m(uu::m(h));
    CHECK(E > 10.0f);
    CHECK(E < 30.0f);  // real roads are lit to about 10 to 30 lux
}

TEST_CASE("L1: inverse-square falloff works in metres not world units", "[lighting][photometry][L1]") {
    CHECK(ul::inverse_square_m(uu::m(1.0f)) == Approx(1.0f));
    CHECK(ul::inverse_square_m(uu::m(10.0f)) == Approx(0.01f));
    CHECK(ul::inverse_square_m(uu::m(2.0f)) == Approx(0.25f * ul::inverse_square_m(uu::m(1.0f))));
    CHECK(ul::inverse_square_m(0.0f) == Approx(1.0e4f));  // floored at 1 cm, never infinite
}

TEST_CASE("L6: colour temperature tints", "[lighting][photometry][L6]") {
    const glm::vec3 daylight = ul::kelvin_to_rgb(6500.0f);
    CHECK(daylight.r == Approx(1.0f).margin(0.1));
    CHECK(daylight.g == Approx(1.0f).margin(0.1));
    CHECK(daylight.b == Approx(1.0f).margin(0.1));

    const glm::vec3 sodium = ul::kelvin_to_rgb(2100.0f);  // 1990s expressway lamp
    CHECK(sodium.r == Approx(1.0f));
    CHECK(sodium.g > 0.35f);
    CHECK(sodium.g < 0.75f);
    CHECK(sodium.b < 0.25f);

    const glm::vec3 overcast = ul::kelvin_to_rgb(10000.0f);
    CHECK(overcast.b == Approx(1.0f));
    CHECK(overcast.r < 0.9f);
}

TEST_CASE("L5: emissive strength survives an 8-bit channel", "[.learning][lighting][photometry][L5]") {
    CHECK(ul::emissive_encode(0.0f) == 0.0f);
    CHECK(ul::emissive_encode(ul::kEmissiveMaxNits) == Approx(1.0f));
    CHECK(ul::emissive_encode(1.0e9f) == Approx(1.0f));  // clamped
    CHECK(ul::emissive_encode(200.0f) > ul::emissive_encode(100.0f));
    for (float nits : {10.0f, 500.0f, 20000.0f, 100000.0f}) {
        const float stored = std::round(ul::emissive_encode(nits) * 255.0f) / 255.0f;  // what UNORM8 keeps
        CHECK(ul::emissive_decode(stored) == Approx(nits).epsilon(0.05));
    }
}
