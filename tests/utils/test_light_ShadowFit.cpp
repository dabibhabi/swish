// utils/light/ShadowFit.h, the learning scaffold for tasks/todo.md "Lighting" phases L3 and L7.
// Hidden ([.learning]): `make test` skips them; run
// `build/tests/utils/swish_utils_tests "[learning][shadow_fit]"`. They fail until the stubs are filled in.
// When a phase passes, drop `.learning` from its tags so ctest guards it.
#include "utils/light/ShadowFit.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace ul = swish::util::light;
using Catch::Approx;

namespace {
bool contains(const std::vector<int>& v, int i) { return std::find(v.begin(), v.end(), i) != v.end(); }
}  // namespace

TEST_CASE("L3: the light up vector never lines up with the light", "[lighting][shadow_fit][L3]") {
    const glm::vec3 dirs[] = {
        {0.0f, 1.0f, 0.0f},  // sun at the zenith: the case that blacks out the road today
        {0.0f, -1.0f, 0.0f},
        glm::normalize(glm::vec3(0.001f, 1.0f, 0.0f)),
        glm::normalize(glm::vec3(0.85f, 0.28f, 0.20f)),  // the shipped golden-hour sun
        {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    for (const glm::vec3& d : dirs) {
        const glm::vec3 up = ul::stable_light_up(d);
        CHECK(glm::length(up) == Approx(1.0f));
        CHECK(std::abs(glm::dot(up, d)) < 0.99f);
        const glm::mat4 view = glm::lookAt(d * 100.0f, glm::vec3(0.0f), up);
        bool            ok   = true;
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) ok = ok && std::isfinite(view[c][r]);
        CHECK(ok);
    }
}

TEST_CASE("L3: world up is kept while the sun is low", "[lighting][shadow_fit][L3]") {
    // Changing the up vector rotates the whole shadow map, so only do it near the zenith.
    CHECK(ul::stable_light_up(glm::normalize(glm::vec3(0.85f, 0.28f, 0.20f))) == glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("L3: snapping lands on the texel grid", "[lighting][shadow_fit][L3]") {
    const float     texel = 0.25f;
    const glm::vec3 p     = ul::snap_to_texel({0.37f, -0.37f, 5.5f}, texel);
    CHECK(p.x == Approx(0.25f));
    CHECK(p.y == Approx(-0.5f));  // rounds DOWN, also for negatives
    CHECK(p.z == 5.5f);
    const glm::vec3 again = ul::snap_to_texel(p, texel);
    CHECK(again.x == Approx(p.x));
    CHECK(again.y == Approx(p.y));
}

TEST_CASE("L3: bias grows with the cascade texel size", "[lighting][shadow_fit][L3]") {
    CHECK(ul::cascade_bias(0.0018f, 10.0f, 10.0f) == Approx(0.0018f));
    CHECK(ul::cascade_bias(0.0018f, 40.0f, 10.0f) == Approx(0.0072f));
}

TEST_CASE("L7: the nearest lights cast shadows", "[.learning][lighting][shadow_fit][L7]") {
    const std::vector<glm::vec3> lamps = {{100, 0, 0}, {10, 0, 0}, {50, 0, 0}, {20, 0, 0}};
    const std::vector<int>       pick  = ul::pick_shadow_casters(lamps, glm::vec3(0.0f), {}, 2);
    REQUIRE(pick.size() == 2);
    CHECK(contains(pick, 1));
    CHECK(contains(pick, 3));
}

TEST_CASE("L7: never more casters than lights or slots", "[.learning][lighting][shadow_fit][L7]") {
    const std::vector<glm::vec3> lamps = {{10, 0, 0}, {20, 0, 0}};
    CHECK(ul::pick_shadow_casters(lamps, glm::vec3(0.0f), {}, 4).size() == 2);
    CHECK(ul::pick_shadow_casters(lamps, glm::vec3(0.0f), {}, 0).empty());
    CHECK(ul::pick_shadow_casters({}, glm::vec3(0.0f), {}, 4).empty());
}

TEST_CASE("L7: a slightly closer lamp does not steal the shadow", "[.learning][lighting][shadow_fit][L7]") {
    const std::vector<glm::vec3> lamps    = {{50, 0, 0}, {45, 0, 0}};  // 45 is not below 0.8 * 50
    const std::vector<int>       previous = {0};
    const std::vector<int>       pick     = ul::pick_shadow_casters(lamps, glm::vec3(0.0f), previous, 1, 0.8f);
    REQUIRE(pick.size() == 1);
    CHECK(pick[0] == 0);
}

TEST_CASE("L7: a much closer lamp takes over", "[.learning][lighting][shadow_fit][L7]") {
    const std::vector<glm::vec3> lamps    = {{50, 0, 0}, {30, 0, 0}};  // 30 is below 0.8 * 50
    const std::vector<int>       previous = {0};
    const std::vector<int>       pick     = ul::pick_shadow_casters(lamps, glm::vec3(0.0f), previous, 1, 0.8f);
    REQUIRE(pick.size() == 1);
    CHECK(pick[0] == 1);
}

TEST_CASE("L7: stale previous indices are ignored", "[.learning][lighting][shadow_fit][L7]") {
    const std::vector<glm::vec3> lamps    = {{10, 0, 0}};
    const std::vector<int>       previous = {7, -1};
    const std::vector<int>       pick     = ul::pick_shadow_casters(lamps, glm::vec3(0.0f), previous, 2);
    REQUIRE(pick.size() == 1);
    CHECK(pick[0] == 0);
}
