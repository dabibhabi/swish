// utils/geometry/Frustum.h: plane extraction + sphere test.
#include "utils/geometry/Frustum.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ug = swish::util::geometry;
using Catch::Matchers::WithinAbs;

namespace {
// Camera at the origin looking down -Z (GLM_FORCE_DEPTH_ZERO_TO_ONE is defined for the test target).
glm::mat4 test_vp() {
    const glm::mat4 view = glm::mat4(1.0f);
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 100.0f);
    return proj * view;
}
}  // namespace

TEST_CASE("extract_frustum_planes returns unit-length normals", "[frustum]") {
    glm::vec4 planes[6];
    ug::extract_frustum_planes(test_vp(), planes);
    for (const glm::vec4& p : planes) CHECK_THAT(glm::length(glm::vec3(p)), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("sphere_outside_frustum: inside behind beyond far and off to the side", "[frustum]") {
    glm::vec4 planes[6];
    ug::extract_frustum_planes(test_vp(), planes);
    CHECK_FALSE(ug::sphere_outside_frustum(planes, {0.0f, 0.0f, -10.0f}, 1.0f));  // in front
    CHECK(ug::sphere_outside_frustum(planes, {0.0f, 0.0f, 10.0f}, 1.0f));         // behind the camera
    CHECK(ug::sphere_outside_frustum(planes, {0.0f, 0.0f, -200.0f}, 1.0f));       // beyond far
    CHECK(ug::sphere_outside_frustum(planes, {500.0f, 0.0f, -10.0f}, 1.0f));      // far to the right
    CHECK(ug::sphere_outside_frustum(planes, {0.0f, -500.0f, -10.0f}, 1.0f));     // far below
}

TEST_CASE("sphere_outside_frustum keeps a sphere that straddles a plane", "[frustum]") {
    glm::vec4 planes[6];
    ug::extract_frustum_planes(test_vp(), planes);
    // Centre just past the far plane but radius reaches back inside.
    CHECK_FALSE(ug::sphere_outside_frustum(planes, {0.0f, 0.0f, -101.0f}, 5.0f));
    // Centre at the near plane depth: touches it.
    CHECK_FALSE(ug::sphere_outside_frustum(planes, {0.0f, 0.0f, -0.5f}, 1.0f));
    // A huge radius sphere centred behind the camera reaches into the frustum.
    CHECK_FALSE(ug::sphere_outside_frustum(planes, {0.0f, 0.0f, 20.0f}, 50.0f));
}
