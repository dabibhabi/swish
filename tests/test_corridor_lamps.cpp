// Street-lamp placement (scene/Corridor/CorridorLamps.h), the learning scaffold for tasks/todo.md
// "Lighting" phase L6.1. Hidden ([.learning]): `make test` skips them; run
// `build/tests/swish_tests "[learning][corridor_lamps]"`. They fail until the stub is filled in.
// When they pass, drop `.learning` from the tags so ctest guards them.

#include "scene/Corridor/CorridorLamps.h"
#include "utils/units/Units.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace swish;
using Catch::Approx;
namespace uu = swish::util::units;

namespace {
LampRowParams row() {
    LampRowParams p;
    p.spacing       = uu::ft(500.0f);  // typical highway spacing, as on the retired road
    p.lateralOffset = uu::m(12.0f);
    p.poleHeight    = uu::ft(30.0f);
    p.armLen        = uu::ft(8.0f);
    return p;
}
}  // namespace

TEST_CASE("L6: lamps every spacing along a straight road", "[lighting][corridor_lamps][L6]") {
    // 1 km straight toward -Z. 500 ft = 152.4 m, so posts at 152.4, 304.8, ... 914.4 m: six posts.
    const std::vector<glm::vec3> line  = {{0, 0, 0}, {0, 0, -uu::m(1000.0f)}};
    const std::vector<LampPost>  posts = lamp_posts_along(line, row());
    REQUIRE(posts.size() == 6);
    CHECK(posts[0].base.z == Approx(-uu::ft(500.0f)));
    CHECK(posts[5].base.z == Approx(-uu::ft(3000.0f)));
}

TEST_CASE("L6: pole to the right of travel and head back over the road", "[lighting][corridor_lamps][L6]") {
    const std::vector<glm::vec3> line  = {{0, 0, 0}, {0, 0, -uu::m(1000.0f)}};
    const LampRowParams          p     = row();
    const std::vector<LampPost>  posts = lamp_posts_along(line, p);
    REQUIRE_FALSE(posts.empty());
    const LampPost& post = posts[0];
    CHECK(post.base.x == Approx(p.lateralOffset));  // travelling -Z with Y up, right is +X
    CHECK(post.base.y == Approx(0.0f).margin(1e-3));
    CHECK(post.head.x == Approx(p.lateralOffset - p.armLen));
    CHECK(post.head.y == Approx(p.poleHeight));
    CHECK(post.head.z == Approx(post.base.z));
    CHECK(post.aim.y == Approx(-1.0f));
}

TEST_CASE("L6: a negative offset puts the row on the left", "[lighting][corridor_lamps][L6]") {
    const std::vector<glm::vec3> line = {{0, 0, 0}, {0, 0, -uu::m(1000.0f)}};
    LampRowParams                p    = row();
    p.lateralOffset                   = -p.lateralOffset;
    const std::vector<LampPost> posts = lamp_posts_along(line, p);
    REQUIRE_FALSE(posts.empty());
    CHECK(posts[0].base.x == Approx(p.lateralOffset));
    CHECK(posts[0].head.x == Approx(p.lateralOffset + p.armLen));  // the arm still reaches toward the road
}

TEST_CASE("L6: lamps follow a bend", "[lighting][corridor_lamps][L6]") {
    // 200 m toward -Z, then 400 m toward +X. Posts at 152.4 m (first leg), 304.8 m and 457.2 m (second leg).
    const std::vector<glm::vec3> line  = {{0, 0, 0}, {0, 0, -uu::m(200.0f)}, {uu::m(400.0f), 0, -uu::m(200.0f)}};
    const LampRowParams          p     = row();
    const std::vector<LampPost>  posts = lamp_posts_along(line, p);
    REQUIRE(posts.size() == 3);
    // On the second leg the travel direction is +X, so right is +Z.
    CHECK(posts[1].base.x == Approx(uu::ft(1000.0f) - uu::m(200.0f)));
    CHECK(posts[1].base.z == Approx(-uu::m(200.0f) + p.lateralOffset));
}

TEST_CASE("L6: degenerate input gives no lamps", "[lighting][corridor_lamps][L6]") {
    const std::vector<glm::vec3> one = {{0, 0, 0}};
    CHECK(lamp_posts_along(one, row()).empty());
    CHECK(lamp_posts_along({}, row()).empty());
    const std::vector<glm::vec3> line = {{0, 0, 0}, {0, 0, -uu::m(1000.0f)}};
    LampRowParams                p    = row();
    p.spacing                         = 0.0f;
    CHECK(lamp_posts_along(line, p).empty());
}
