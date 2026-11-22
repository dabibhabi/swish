// utils/geometry/Polyline.h: arc-length queries.
#include "utils/geometry/Polyline.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

namespace ug = swish::util::geometry;
using Catch::Matchers::WithinAbs;

namespace {
// L-shaped path: 10 along -Z, then 5 along +X. Total length 15.
const std::vector<glm::vec3> kL{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f}, {5.0f, 0.0f, -10.0f}};
}  // namespace

TEST_CASE("polyline_length sums segment lengths", "[polyline]") {
    CHECK_THAT(ug::polyline_length(kL), WithinAbs(15.0f, 1e-5f));
    CHECK(ug::polyline_length(std::vector<glm::vec3>{}) == 0.0f);
    CHECK(ug::polyline_length(std::vector<glm::vec3>{{1.0f, 2.0f, 3.0f}}) == 0.0f);
}

TEST_CASE("polyline_sample returns position and unit tangent along the path", "[polyline]") {
    glm::vec3 pos, tan;
    ug::polyline_sample(kL, 4.0f, pos, tan);
    CHECK_THAT(pos.z, WithinAbs(-4.0f, 1e-5f));
    CHECK_THAT(tan.z, WithinAbs(-1.0f, 1e-6f));

    ug::polyline_sample(kL, 12.0f, pos, tan);  // 2 m into the second segment
    CHECK_THAT(pos.x, WithinAbs(2.0f, 1e-5f));
    CHECK_THAT(pos.z, WithinAbs(-10.0f, 1e-5f));
    CHECK_THAT(tan.x, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("polyline_sample clamps at both ends and handles degenerate inputs", "[polyline]") {
    glm::vec3 pos, tan;
    ug::polyline_sample(kL, -3.0f, pos, tan);  // before the start
    CHECK(pos == glm::vec3(0.0f));
    ug::polyline_sample(kL, 999.0f, pos, tan);  // past the end: last point, last tangent
    CHECK_THAT(pos.x, WithinAbs(5.0f, 1e-5f));
    CHECK_THAT(tan.x, WithinAbs(1.0f, 1e-6f));

    ug::polyline_sample(std::vector<glm::vec3>{}, 1.0f, pos, tan);
    CHECK(pos == glm::vec3(0.0f));
    CHECK(tan == glm::vec3(0.0f, 0.0f, -1.0f));
    const std::vector<glm::vec3> one{{7.0f, 8.0f, 9.0f}};
    ug::polyline_sample(one, 1.0f, pos, tan);
    CHECK(pos == glm::vec3(7.0f, 8.0f, 9.0f));
}
