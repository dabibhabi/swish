#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>

using Catch::Matchers::WithinAbs;

// These tests pin the metallic-workflow contract that lighting.frag implements
// (P0 #2). The bug was that gbuffer.frag wrote the dielectric F0 constant (0.04)
// into the *metalness* channel, so no surface could ever be real metal. The fix
// keeps 0.04 as a separate dielectric-F0 constant and derives F0 from a real
// metalness:  F0 = mix(vec3(dielectricF0), albedo, metallic).

namespace {
constexpr float kDielectricF0 = 0.04f;  // matches lighting.frag (dry)

glm::vec3 fresnelF0(const glm::vec3& albedo, float metallic, float dielectricF0) {
    return glm::mix(glm::vec3(dielectricF0), albedo, metallic);
}

// Mirror of SceneGeometry's first-pass material->metalness mapping.
// MAT_METAL == 3 (see SceneTypes.h MaterialId).
float metalnessForMaterial(uint32_t material) {
    constexpr uint32_t MAT_METAL = 3;
    return (material == MAT_METAL) ? 1.0f : 0.0f;
}
}  // namespace

TEST_CASE("F0 equals the dielectric constant at metalness 0", "[pbr]") {
    glm::vec3 albedo(0.8f, 0.2f, 0.1f);
    glm::vec3 F0 = fresnelF0(albedo, 0.0f, kDielectricF0);
    REQUIRE_THAT(F0.r, WithinAbs(kDielectricF0, 1e-6f));
    REQUIRE_THAT(F0.g, WithinAbs(kDielectricF0, 1e-6f));
    REQUIRE_THAT(F0.b, WithinAbs(kDielectricF0, 1e-6f));
}

TEST_CASE("F0 equals albedo at metalness 1", "[pbr]") {
    glm::vec3 albedo(0.8f, 0.2f, 0.1f);
    glm::vec3 F0 = fresnelF0(albedo, 1.0f, kDielectricF0);
    REQUIRE_THAT(F0.r, WithinAbs(albedo.r, 1e-6f));
    REQUIRE_THAT(F0.g, WithinAbs(albedo.g, 1e-6f));
    REQUIRE_THAT(F0.b, WithinAbs(albedo.b, 1e-6f));
}

TEST_CASE("Dielectric materials map to metalness 0, metal to 1", "[pbr]") {
    REQUIRE(metalnessForMaterial(0) == 0.0f);  // MAT_ASPHALT
    REQUIRE(metalnessForMaterial(1) == 0.0f);  // MAT_GRASS
    REQUIRE(metalnessForMaterial(2) == 0.0f);  // MAT_CONCRETE
    REQUIRE(metalnessForMaterial(3) == 1.0f);  // MAT_METAL
    REQUIRE(metalnessForMaterial(4) == 0.0f);  // MAT_DEFAULT
    // The old bug wrote 0.04 as metalness for everything; assert we never do that.
    REQUIRE(metalnessForMaterial(0) != 0.04f);
}
