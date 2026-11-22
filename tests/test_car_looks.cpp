// scene/Entity/CarLooks.h: paint looks and by-name material fixes for the car showcase. GPU-free.
#include "scene/Entity/CarLooks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace swish;
using Catch::Approx;

TEST_CASE("Showcase: 'as the asset' and clean leaves the paint alone", "[showcase][car_looks]") {
    const PaintBase        base{0.31f, 0.35f, 0.3f};
    const MaterialOverride o = paint_look(PaintFinish::Asset, PaintColour::Asset, CarSurface::Clean, base);
    CHECK_FALSE(o.enabled);
    CHECK(o.metalness == Approx(0.31f));  // and if it were applied it would still be the asset's values
    CHECK(o.roughnessMul == Approx(1.0f));
    CHECK(o.color == Vec3(1.0f));
    CHECK_FALSE(
        paint_look(PaintFinish::Asset, PaintColour::Asset, CarSurface::Wet, base).enabled);  // wet is a per-draw flag
}

TEST_CASE("Showcase: finishes go from mirror to matte", "[showcase][car_looks]") {
    const auto look = [](PaintFinish f) { return paint_look(f, PaintColour::Asset, CarSurface::Clean); };
    CHECK(look(PaintFinish::SolidGloss).enabled);
    CHECK(look(PaintFinish::SolidGloss).flakeDensity == 0.0f);
    CHECK(look(PaintFinish::Metallic).flakeDensity > look(PaintFinish::Pearl).flakeDensity);
    CHECK(look(PaintFinish::Metallic).metalness > look(PaintFinish::Pearl).metalness);
    CHECK(look(PaintFinish::Satin).roughnessMul > look(PaintFinish::SolidGloss).roughnessMul);
    CHECK(look(PaintFinish::Matte).roughnessMul > look(PaintFinish::Satin).roughnessMul);
    CHECK(look(PaintFinish::Matte).clearcoat == 0.0f);  // no clear coat on matte
    CHECK(look(PaintFinish::SolidGloss).clearcoat > look(PaintFinish::Satin).clearcoat);
}

TEST_CASE("Showcase: colours are plausible albedos and a dusty car is duller", "[showcase][car_looks]") {
    for (int c = 1; c < static_cast<int>(PaintColour::Count); ++c) {
        const Vec3 col = paint_look(PaintFinish::SolidGloss, static_cast<PaintColour>(c), CarSurface::Clean).color;
        CHECK(glm::all(glm::greaterThanEqual(col, Vec3(0.0f))));
        CHECK(glm::all(glm::lessThanEqual(col, Vec3(0.9f))));  // nothing reflects more than about 90%
    }
    const Vec3 red = paint_look(PaintFinish::SolidGloss, PaintColour::Red, CarSurface::Clean).color;
    CHECK(red.r > 5.0f * red.g);
    const MaterialOverride clean = paint_look(PaintFinish::Metallic, PaintColour::Black, CarSurface::Clean);
    const MaterialOverride dusty = paint_look(PaintFinish::Metallic, PaintColour::Black, CarSurface::Dusty);
    CHECK(dusty.roughnessMul > clean.roughnessMul);
    CHECK(dusty.clearcoat < clean.clearcoat);
    CHECK(dusty.color.r > clean.color.r);  // dust lightens a black car
    CHECK(paint_look(PaintFinish::Asset, PaintColour::Asset, CarSurface::Dusty).enabled);
}

TEST_CASE("Showcase: material fixes by name", "[showcase][car_looks]") {
    // The rigged Porsche's names (assets/blend/porsche_rigged.glb).
    CHECK(car_material_fix("rsVehicle_Exterior_mm_tyre1").metalness == 0.0f);  // the asset says 0.62
    CHECK(car_material_fix("rsVehicle_Exterior_mm_tyre1").roughness == Approx(0.85f));
    CHECK(car_material_fix("rsVehicle_Exterior_mm_windows1").metalness == 0.0f);
    CHECK(car_material_fix("light_glass").roughness == Approx(0.05f));
    CHECK(car_material_fix("rsVehicle_Exterior_mm_rotor1").metalness == 1.0f);
    CHECK(car_material_fix("rsVehicle_Exterior_mm_wheel1").metalness == Approx(0.9f));
    CHECK(car_material_fix("int1").metalness == 0.0f);      // cabin
    CHECK(car_material_fix("int1").roughness < 0.0f);       // keeps the asset's roughness
    CHECK(car_material_fix("costuras").metalness == 0.0f);  // stitching
    // Body paint is never touched, even though "paint" contains "int".
    CHECK(car_material_fix("rsVehicle_Exterior_mm_ext1").metalness < 0.0f);
    CHECK(car_material_fix("HELCAT_MAT_Paint").metalness < 0.0f);
    CHECK(car_material_fix("Porsche_911GT3_Paint_Material").metalness < 0.0f);
    // Unknown names are left alone; matching ignores case.
    CHECK(car_material_fix("rsVehicle_Exterior_mm_lights1").metalness < 0.0f);
    CHECK(car_material_fix("WHEEL_CHROME").metalness == 1.0f);
    CHECK(car_material_fix("Wheel_dinghyblack.001").metalness == Approx(0.9f));
}
