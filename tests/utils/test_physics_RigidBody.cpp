#include "utils/physics/RigidBody.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

namespace up = swish::util::physics;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using swish::mat3;
using swish::quat;
using swish::vec3;

namespace {
constexpr float kH = 1.0f / 240.0f;

up::RigidBody make_body() {
    up::RigidBody b;
    b.set_mass_properties(1670.f, up::RigidBodyParams{}.box_inertia());
    return b;
}
}  // namespace

TEST_CASE("RigidBodyParams box inertia is diagonal, positive and near a 911's yaw inertia", "[rigidbody]") {
    const mat3 i = up::RigidBodyParams{}.box_inertia();
    REQUIRE(i[0][0] > 0.f);
    REQUIRE(i[1][1] > 0.f);
    REQUIRE(i[2][2] > 0.f);
    REQUIRE(i[0][1] == 0.f);
    REQUIRE(i[1][2] == 0.f);
    // Solid box about the vertical axis: m/12 (l^2 + w^2) ~ 3.4e3 kg m^2 (TireParams::iz = 3050 measured-ish).
    REQUIRE_THAT(i[1][1], WithinRel(3050.f, 0.2f));
}

TEST_CASE("free fall follows the semi-implicit Euler solution and stays upright", "[rigidbody]") {
    up::RigidBody b = make_body();
    b.set_position(vec3(0.f, 10.f, 0.f));
    constexpr float g = 9.81f;
    const int       n = 240;  // 1 s
    for (int i = 0; i < n; ++i) {
        b.add_force(vec3(0.f, -b.mass() * g, 0.f));
        b.step(kH);
    }
    // Velocity-first Euler: v_n = -g n h exactly; y_n = y0 - g h^2 n(n+1)/2 (O(h) above the analytic y0 - g t^2/2).
    REQUIRE_THAT(b.linear_velocity().y, WithinAbs(-g * 1.0f, 1e-3f));
    const float discrete = 10.f - g * kH * kH * 0.5f * n * (n + 1);
    REQUIRE_THAT(b.position().y, WithinAbs(discrete, 1e-3f));
    REQUIRE_THAT(b.position().y, WithinAbs(10.f - 0.5f * g, 0.03f));  // analytic, within O(h)
    REQUIRE_THAT(b.position().x, WithinAbs(0.f, 1e-6f));
    REQUIRE_THAT(glm::length(b.angular_velocity()), WithinAbs(0.f, 1e-6f));
}

TEST_CASE("accumulators are cleared by step and a force at a point adds its moment", "[rigidbody]") {
    up::RigidBody b = make_body();
    b.add_force_at_point(vec3(0.f, 100.f, 0.f), vec3(2.f, 0.f, 0.f));  // lever +X, force +Y => torque +Z
    REQUIRE_THAT(b.torque_accumulator().z, WithinAbs(200.f, 1e-4f));
    REQUIRE_THAT(b.force_accumulator().y, WithinAbs(100.f, 1e-6f));
    b.step(kH);
    REQUIRE(b.force_accumulator() == vec3(0.f));
    REQUIRE(b.torque_accumulator() == vec3(0.f));
    REQUIRE(b.angular_velocity().z > 0.f);  // spun about +Z (right-hand rule: +X lever, +Y push)
}

TEST_CASE("torque-free spin of an asymmetric body conserves angular momentum", "[rigidbody]") {
    up::RigidBody b = make_body();
    // Mixed-axis spin: exercises the gyroscopic term (w and L are NOT parallel for an asymmetric body).
    b.set_angular_velocity(vec3(2.0f, 0.3f, 1.5f));
    // Recompute L from the public (w, pose) state rather than reading the carried L: a real check.
    const auto  l_of = [](const up::RigidBody& rb) { return rb.inertia_world() * rb.angular_velocity(); };
    const vec3  l0   = l_of(b);
    const float e0   = b.kinetic_energy();
    for (int i = 0; i < 240 * 10; ++i)  // 10 s
        b.step(kH);
    const vec3 l1 = l_of(b);
    REQUIRE_THAT(glm::length(l1), WithinRel(glm::length(l0), 1e-4f));
    REQUIRE(glm::length(l1 - l0) < 1e-3f * glm::length(l0));  // direction too, not just magnitude
    REQUIRE_THAT(b.kinetic_energy(), WithinRel(e0, 2e-3f));
    REQUIRE_THAT(glm::length(b.orientation()), WithinAbs(1.f, 1e-5f));
}

TEST_CASE("spin about a principal axis stays a pure spin", "[rigidbody]") {
    up::RigidBody b = make_body();
    b.set_angular_velocity(vec3(0.f, 3.f, 0.f));  // yaw axis
    for (int i = 0; i < 240; ++i) b.step(kH);
    REQUIRE_THAT(b.angular_velocity().y, WithinAbs(3.f, 1e-4f));
    REQUIRE_THAT(b.angular_velocity().x, WithinAbs(0.f, 1e-4f));
    REQUIRE_THAT(b.angular_velocity().z, WithinAbs(0.f, 1e-4f));
}

TEST_CASE("euler_yxz_from_rotation inverts the engine's Ry Rx Rz model-matrix order", "[rigidbody]") {
    const float pitch = glm::radians(5.f), yaw = glm::radians(-120.f), roll = glm::radians(-3.f);
    const quat  q = glm::angleAxis(yaw, vec3(0.f, 1.f, 0.f)) * glm::angleAxis(pitch, vec3(1.f, 0.f, 0.f)) *
                    glm::angleAxis(roll, vec3(0.f, 0.f, 1.f));
    const vec3  e = up::euler_yxz_from_rotation(glm::mat3_cast(q));
    REQUIRE_THAT(e.x, WithinAbs(pitch, 1e-5f));
    REQUIRE_THAT(e.y, WithinAbs(yaw, 1e-5f));
    REQUIRE_THAT(e.z, WithinAbs(roll, 1e-5f));
}
