// utils/color/Color.h: rgba().
#include "utils/color/Color.h"

#include <catch2/catch_test_macros.hpp>

namespace uc = swish::util::color;

TEST_CASE("rgba copies four floats in order", "[color]") {
    const float     c[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    const glm::vec4 v    = uc::rgba(c);
    CHECK(v.r == 0.1f);
    CHECK(v.g == 0.2f);
    CHECK(v.b == 0.3f);
    CHECK(v.a == 0.4f);
}

TEST_CASE("rgba works on a member array (RoadConfig-style tint array)", "[color]") {
    struct Cfg {
        float tint[4];
    } cfg{{1.0f, 0.5f, 0.25f, 1.0f}};
    CHECK(uc::rgba(cfg.tint) == glm::vec4(1.0f, 0.5f, 0.25f, 1.0f));
}
