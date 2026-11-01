// Chapter 01: GearboxParams invariants as constexpr validators (docs/tutorials/typed-cpp/01-static-assert.md).
#include "scene/Entity/Powertrain.h"

#include <catch2/catch_test_macros.hpp>

using namespace swish;

static_assert(gearbox_params_valid(GearboxParams{}));
static_assert(!gearbox_params_valid(GearboxParams{.upshiftRpm = 3000.f}));
static_assert(kGearCount == 7);

TEST_CASE("GearboxParams defaults satisfy every invariant", "[car][gearbox][invariants]") {
    const GearboxParams g;
    CHECK(ratios_positive(g));
    CHECK(ratios_strictly_descending(g));
    CHECK(shift_band_ordered(g));
    CHECK(dwell_non_negative(g));
    CHECK(upshift_lands_above_downshift(g));
    CHECK(downshift_lands_below_upshift(g));
    CHECK(gearbox_params_valid(g));
}

TEST_CASE("Gearbox validator rejects an inverted shift band", "[car][gearbox][invariants]") {
    const GearboxParams g{.upshiftRpm = 3000.f};
    CHECK_FALSE(shift_band_ordered(g));
    CHECK_FALSE(gearbox_params_valid(g));
}

TEST_CASE("Gearbox validator rejects a negative dwell", "[car][gearbox][invariants]") {
    const GearboxParams g{.shiftDwell = -0.1f};
    CHECK_FALSE(dwell_non_negative(g));
}

TEST_CASE("Gearbox validator rejects a zero or negative ratio", "[car][gearbox][invariants]") {
    GearboxParams g;
    g.ratios[3] = 0.f;
    CHECK_FALSE(ratios_positive(g));
    g.ratios[3] = -1.f;
    CHECK_FALSE(ratios_positive(g));
    GearboxParams h{.finalDrive = 0.f};
    CHECK_FALSE(ratios_positive(h));
}

TEST_CASE("Gearbox validator rejects ratios that do not descend", "[car][gearbox][invariants]") {
    GearboxParams g;
    g.ratios[1] = g.ratios[0];
    CHECK_FALSE(ratios_strictly_descending(g));
}

TEST_CASE("Gearbox validator rejects a band an upshift can land below", "[car][gearbox][invariants]") {
    const GearboxParams g{.downshiftRpm = 5000.f};
    CHECK_FALSE(upshift_lands_above_downshift(g));
}
