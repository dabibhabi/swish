// utils/debug/TomlGlm.h: vec3 <-> TOML array. Built into swish_debug_tests (needs toml++, debug-UI config only).
#include "utils/debug/TomlGlm.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace ud = swish::util::debug;
using Catch::Matchers::WithinAbs;

TEST_CASE("vec3_to_toml writes a 3-element array and read_vec3 round-trips it", "[tomlglm]") {
    const glm::vec3 in{0.25f, -1.5f, 3.0f};
    toml::table     t;
    t.insert("v", ud::vec3_to_toml(in));
    const toml::array* a = t["v"].as_array();
    REQUIRE(a != nullptr);
    CHECK(a->size() == 3);

    glm::vec3 out{9.0f, 9.0f, 9.0f};
    ud::read_vec3(t["v"], out);
    CHECK(out == in);
}

TEST_CASE("read_vec3 keeps the current value when the node is absent or malformed", "[tomlglm]") {
    toml::table t = toml::parse("bad = [1.0, 2.0]\nnum = 4\n");
    glm::vec3   v{1.0f, 2.0f, 3.0f};
    ud::read_vec3(t["missing"], v);
    CHECK(v == glm::vec3(1.0f, 2.0f, 3.0f));
    ud::read_vec3(t["bad"], v);  // wrong length
    CHECK(v == glm::vec3(1.0f, 2.0f, 3.0f));
    ud::read_vec3(t["num"], v);  // not an array
    CHECK(v == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("read_vec3 substitutes the current component for a non-numeric element", "[tomlglm]") {
    toml::table t = toml::parse("v = [5.0, \"x\", 7.0]\n");
    glm::vec3   v{1.0f, 2.0f, 3.0f};
    ud::read_vec3(t["v"], v);
    CHECK_THAT(v.x, WithinAbs(5.0f, 1e-6f));
    CHECK_THAT(v.y, WithinAbs(2.0f, 1e-6f));
    CHECK_THAT(v.z, WithinAbs(7.0f, 1e-6f));
}
