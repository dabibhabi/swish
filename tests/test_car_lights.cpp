// Car lights (tasks/todo.md "Car lights" S1): node-name mapping and the pure
// anchors -> world spot-light transform in scene/Entity/CarLights.h. GPU-free.

#include "scene/Entity/CarLights.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace swish;
using Catch::Approx;

namespace {
std::vector<CarLightAnchor> rig_anchors() {
    // Car space: nose +X, up +Y, metres (the shape ModelManager emits).
    return {
        {CarLightKind::Headlight, Vec3(1.8f, 0.64f, 0.6f), Vec3(1.f, 0.f, 0.f)},
        {CarLightKind::Headlight, Vec3(1.8f, 0.64f, -0.6f), Vec3(1.f, 0.f, 0.f)},
        {CarLightKind::TailLight, Vec3(-1.9f, 0.58f, 0.6f), Vec3(-1.f, 0.f, 0.f)},
        {CarLightKind::BrakeLight, Vec3(-1.95f, 0.59f, 0.5f), Vec3(-1.f, 0.f, 0.f)},
    };
}
}  // namespace

TEST_CASE("car light kind comes from the exact _Lamp node name", "[car_lights]") {
    REQUIRE(car_light_kind_from_node("Headlight_L_Lamp") == CarLightKind::Headlight);
    REQUIRE(car_light_kind_from_node("Headlight_R_Lamp") == CarLightKind::Headlight);
    REQUIRE(car_light_kind_from_node("TailLight_R_Lamp") == CarLightKind::TailLight);
    REQUIRE(car_light_kind_from_node("RearLightBar_Lamp") == CarLightKind::TailLight);
    REQUIRE(car_light_kind_from_node("BrakeLight_L_Lamp") == CarLightKind::BrakeLight);
    // The pivot Empties and lens meshes are not lamps.
    REQUIRE_FALSE(car_light_kind_from_node("Headlight_L").has_value());
    REQUIRE_FALSE(car_light_kind_from_node("Headlight_L_Lens").has_value());
    REQUIRE_FALSE(car_light_kind_from_node("headlight_l_lamp").has_value());
}

TEST_CASE("all lamps off adds no light (identity)", "[car_lights]") {
    const auto lights = car_spot_lights(rig_anchors(), Mat4(1.f), CarLightState{}, CarLightParams{});
    REQUIRE(lights.empty());
}

TEST_CASE("each switch lights only its own lamps", "[car_lights]") {
    const CarLightParams p;
    REQUIRE(car_spot_lights(rig_anchors(), Mat4(1.f), {true, false, 0.f}, p).size() == 2);
    REQUIRE(car_spot_lights(rig_anchors(), Mat4(1.f), {false, true, 0.f}, p).size() == 1);
    const auto brake = car_spot_lights(rig_anchors(), Mat4(1.f), {false, false, 0.5f}, p);
    REQUIRE(brake.size() == 1);
    REQUIRE(brake[0].intensity == Approx(p.brakeIntensity * 0.5f));
}

TEST_CASE("anchors follow the car's model matrix into world space", "[car_lights]") {
    // Car at (100, 0, 200) WU, yawed +90 deg about Y, entity scale 1000 WU/m (AppInit).
    Mat4 model = glm::translate(Mat4(1.f), Vec3(100.f, 0.f, 200.f));
    model      = glm::rotate(model, glm::radians(90.f), Vec3(0.f, 1.f, 0.f));
    model      = glm::scale(model, Vec3(util::units::kWuPerMetre));

    const CarLightParams p;
    const auto           lights = car_spot_lights(rig_anchors(), model, {true, false, 0.f}, p);
    REQUIRE(lights.size() == 2);

    // +90 deg yaw maps car +X (nose) to world -Z.
    const SpotLightDesc& l = lights[0];
    REQUIRE(l.direction.x == Approx(0.f).margin(1e-5));
    REQUIRE(l.direction.z == Approx(-1.f));
    REQUIRE(glm::length(l.direction) == Approx(1.f));
    // Position: (1.8, 0.64, 0.6) m -> rotated (0.6, 0.64, -1.8) m -> WU + offset.
    REQUIRE(l.position.x == Approx(100.f + 600.f));
    REQUIRE(l.position.y == Approx(640.f));
    REQUIRE(l.position.z == Approx(200.f - 1800.f));
    // Range in WU, cone cosines ordered inner > outer.
    REQUIRE(l.range == Approx(util::units::m(p.headRangeM)));
    REQUIRE(l.cosInner > l.cosOuter);
}
