#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "scene/Camera/Camera.h"

using namespace swish;
using Catch::Matchers::WithinAbs;

static constexpr float kEps = 1e-4f;

// ── Forward vector ────────────────────────────────────────────────────

TEST_CASE("Camera default yaw=-90 produces forward=(0,0,-1)", "[camera]") {
    Camera cam;
    cam.set_pitch(0.0f);
    // Default yaw is -90°; header says yaw 0 = -Z, yaw 90 = -X
    // so yaw -90 should give forward along +X? Re-verify with implementation:
    // forward.x = cos(yaw)*cos(pitch), forward.z = sin(yaw)*cos(pitch)
    // yaw=-90°: cos(-90)=0, sin(-90)=-1  →  x=0, z=-1  ✓
    Vec3 fwd = cam.get_forward();
    REQUIRE_THAT(fwd.x, WithinAbs(0.0f, kEps));
    REQUIRE_THAT(fwd.y, WithinAbs(0.0f, kEps));
    REQUIRE_THAT(fwd.z, WithinAbs(-1.0f, kEps));
}

TEST_CASE("Camera yaw=0 pitch=0 produces forward=(1,0,0)", "[camera]") {
    Camera cam;
    cam.set_yaw(0.0f);
    cam.set_pitch(0.0f);
    // cos(0)=1, sin(0)=0  →  x=1, z=0
    Vec3 fwd = cam.get_forward();
    REQUIRE_THAT(fwd.x, WithinAbs(1.0f, kEps));
    REQUIRE_THAT(fwd.y, WithinAbs(0.0f, kEps));
    REQUIRE_THAT(fwd.z, WithinAbs(0.0f, kEps));
}

TEST_CASE("Camera pitch=+45 raises forward y component", "[camera]") {
    Camera cam;
    cam.set_yaw(-90.0f);
    cam.set_pitch(45.0f);
    Vec3 fwd = cam.get_forward();
    REQUIRE(fwd.y > 0.0f);
}

TEST_CASE("Camera forward vector is unit length", "[camera]") {
    Camera cam;
    cam.set_yaw(37.0f);
    cam.set_pitch(-12.0f);
    Vec3 fwd = cam.get_forward();
    float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    REQUIRE_THAT(len, WithinAbs(1.0f, kEps));
}

// ── Right vector ──────────────────────────────────────────────────────

TEST_CASE("Camera right vector is orthogonal to forward", "[camera]") {
    Camera cam;
    cam.set_yaw(45.0f);
    cam.set_pitch(20.0f);
    Vec3 fwd   = cam.get_forward();
    Vec3 right = cam.get_right();
    float dot  = fwd.x * right.x + fwd.y * right.y + fwd.z * right.z;
    REQUIRE_THAT(dot, WithinAbs(0.0f, kEps));
}

TEST_CASE("Camera right vector is unit length", "[camera]") {
    Camera cam;
    Vec3  right = cam.get_right();
    float len   = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    REQUIRE_THAT(len, WithinAbs(1.0f, kEps));
}

// ── Pitch clamping ────────────────────────────────────────────────────

TEST_CASE("Camera pitch clamps at +89 on upward mouse input", "[camera]") {
    Camera cam;
    cam.process_mouse(0.0f, 100000.0f);
    REQUIRE(cam.get_pitch() <= 89.0f);
}

TEST_CASE("Camera pitch clamps at -89 on downward mouse input", "[camera]") {
    Camera cam;
    cam.process_mouse(0.0f, -100000.0f);
    REQUIRE(cam.get_pitch() >= -89.0f);
}

// ── Mouse look ────────────────────────────────────────────────────────

TEST_CASE("Camera process_mouse updates yaw", "[camera]") {
    Camera cam;
    float before_yaw = cam.get_yaw();
    constexpr float mouse_dx = 10.0f;
    cam.process_mouse(mouse_dx, 0.0f);
    // process_mouse: m_yaw += x_offset * m_mouse_sensitivity (0.1f)
    // expected delta = mouse_dx * 0.1f = 1.0f
    float expected_yaw = before_yaw + mouse_dx * 0.1f;
    REQUIRE_THAT(cam.get_yaw(), WithinAbs(expected_yaw, 0.01f));
}

TEST_CASE("Camera process_mouse updates pitch", "[camera]") {
    Camera cam;
    float before_pitch = cam.get_pitch();
    constexpr float mouse_dy = 5.0f;
    cam.process_mouse(0.0f, mouse_dy);
    // process_mouse: m_pitch += y_offset * m_mouse_sensitivity (0.1f)
    // expected delta = mouse_dy * 0.1f = 0.5f; default pitch is -5.0f so result is -4.5f (within clamp)
    float expected_pitch = before_pitch + mouse_dy * 0.1f;
    REQUIRE_THAT(cam.get_pitch(), WithinAbs(expected_pitch, 0.01f));
}

// ── Projection matrix ─────────────────────────────────────────────────

TEST_CASE("Camera projection encodes FOV at 90 degrees", "[camera]") {
    Camera cam;
    cam.set_perspective(90.0f, 1.0f, 1.0f, 1000.0f);
    Mat4  proj    = cam.get_projection_matrix();
    // At FOV=90°, aspect=1: proj[0][0] = 1/tan(45°) = 1.0
    REQUIRE_THAT(proj[0][0], WithinAbs(1.0f, 1e-3f));
}

TEST_CASE("Camera projection encodes aspect ratio", "[camera]") {
    Camera cam;
    cam.set_perspective(90.0f, 2.0f, 1.0f, 1000.0f);
    Mat4 proj = cam.get_projection_matrix();
    // aspect=2: proj[0][0] = 1/(tan(45°)*2) = 0.5
    REQUIRE_THAT(proj[0][0], WithinAbs(0.5f, 1e-3f));
}

// ── Position/orientation setters ──────────────────────────────────────

TEST_CASE("Camera set_position and get_position round-trip", "[camera]") {
    Camera cam;
    cam.set_position({1.0f, 2.0f, 3.0f});
    Vec3 pos = cam.get_position();
    REQUIRE_THAT(pos.x, WithinAbs(1.0f, kEps));
    REQUIRE_THAT(pos.y, WithinAbs(2.0f, kEps));
    REQUIRE_THAT(pos.z, WithinAbs(3.0f, kEps));
}

TEST_CASE("Camera set_yaw and get_yaw round-trip", "[camera]") {
    Camera cam;
    cam.set_yaw(42.0f);
    REQUIRE_THAT(cam.get_yaw(), WithinAbs(42.0f, kEps));
}

TEST_CASE("Camera set_pitch and get_pitch round-trip", "[camera]") {
    Camera cam;
    cam.set_pitch(-15.0f);
    REQUIRE_THAT(cam.get_pitch(), WithinAbs(-15.0f, kEps));
}

// ── Projection depth convention (Vulkan [0,1] NDC-Z) ──────────────────
// Guards the GLM_FORCE_DEPTH_ZERO_TO_ONE fix (P0 #1). GLM's perspective
// (camera looks down -Z) must map the near plane to NDC-Z 0 and the far
// plane to NDC-Z 1. Without the macro GLM emits OpenGL's [-1,1] and the near
// plane would map to -1, corrupting all depth->world reconstruction.
TEST_CASE("Camera projection uses Vulkan [0,1] depth convention", "[camera][depth]") {
    Camera cam;
    cam.set_perspective(60.0f, 16.0f / 9.0f, 1.0f, 100.0f);
    Mat4 proj = cam.get_projection_matrix();  // Y-flip only touches row 1, not Z

    auto ndcZ = [&](float viewZ) {
        Vec4 clip = proj * Vec4(0.0f, 0.0f, viewZ, 1.0f);
        return clip.z / clip.w;
    };
    REQUIRE_THAT(ndcZ(-1.0f),   WithinAbs(0.0f, 1e-3f));  // near plane -> 0
    REQUIRE_THAT(ndcZ(-100.0f), WithinAbs(1.0f, 1e-3f));  // far  plane -> 1
}

// Round-trip a known world point exactly as lighting.frag does:
// clip = proj*view*p ; ndc = clip/w ; recon = invView * (invProj * ndc)/w.
// Self-consistent under any convention, but combined with the test above it
// proves the runtime reconstruction recovers the correct world position.
TEST_CASE("Camera proj/view round-trips a world point via the inverse", "[camera][depth]") {
    Camera cam;
    cam.set_position(Vec3(0.0f, 0.0f, 0.0f));
    cam.set_yaw(-90.0f);   // forward = (0,0,-1)
    cam.set_pitch(0.0f);
    cam.set_perspective(65.0f, 16.0f / 9.0f, 1.0f, 1000.0f);

    Mat4 view = cam.get_view_matrix();
    Mat4 proj = cam.get_projection_matrix();

    Vec3 worldP(2.0f, -1.0f, -50.0f);  // in front of the camera (-Z), off-axis
    Vec4 clip = proj * view * Vec4(worldP, 1.0f);
    Vec3 ndc  = Vec3(clip) / clip.w;

    Vec4 viewPos = glm::inverse(proj) * Vec4(ndc, 1.0f);
    viewPos /= viewPos.w;
    Vec4 recon = glm::inverse(view) * viewPos;

    REQUIRE_THAT(recon.x, WithinAbs(worldP.x, 1e-2f));
    REQUIRE_THAT(recon.y, WithinAbs(worldP.y, 1e-2f));
    REQUIRE_THAT(recon.z, WithinAbs(worldP.z, 1e-2f));
}
