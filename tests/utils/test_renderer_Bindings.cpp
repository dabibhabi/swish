// renderer/Bindings.h re-exports shaders/common/bindings.glsl. Pin the numbers the shaders and host must agree on:
// changing one without the other breaks a descriptor binding silently on MoltenVK.
#include "renderer/Bindings.h"

#include <catch2/catch_test_macros.hpp>

namespace bind = swish::bind;

TEST_CASE("descriptor set indices match the documented layout", "[renderer]") {
    STATIC_REQUIRE(bind::kSetCamera == 0);
    STATIC_REQUIRE(bind::kSetGbuffer == 1);
    STATIC_REQUIRE(bind::kSetShadow == 2);
    STATIC_REQUIRE(bind::kSetIbl == 3);
    STATIC_REQUIRE(bind::kSetSceneParams == 4);
}

TEST_CASE("G-buffer bindings keep depth at 3 and clearcoat+flake at 4", "[renderer]") {
    STATIC_REQUIRE(bind::kBindGbAlbedo == 0);
    STATIC_REQUIRE(bind::kBindGbNormal == 1);
    STATIC_REQUIRE(bind::kBindGbMaterial == 2);
    STATIC_REQUIRE(bind::kBindGbDepth == 3);
    STATIC_REQUIRE(bind::kBindGbClearcoatFlake == 4);
}

TEST_CASE("composite inputs are bindings 0..4 of set 0", "[renderer]") {
    STATIC_REQUIRE(bind::kSetComposite == 0);
    STATIC_REQUIRE(bind::kBindCompHdr == 0);
    STATIC_REQUIRE(bind::kBindCompBloom == 1);
    STATIC_REQUIRE(bind::kBindCompAo == 2);
    STATIC_REQUIRE(bind::kBindCompSsr == 3);
    STATIC_REQUIRE(bind::kBindCompGodrays == 4);
}
