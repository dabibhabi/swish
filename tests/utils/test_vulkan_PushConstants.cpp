// utils/vulkan/PushConstants.h: the 16-byte rule and the device-limit check.
#include "utils/vulkan/PushConstants.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

namespace {
struct Block32 {
    float a[8];
};
static_assert(sizeof(Block32) % 16 == 0);
// push_range<struct with size % 16 != 0> must not compile (checked by the static_assert in the header).
}  // namespace

TEST_CASE("push_range builds a matching VkPushConstantRange", "[vulkan]") {
    constexpr VkPushConstantRange r = vkx::push_range<Block32>(VK_SHADER_STAGE_FRAGMENT_BIT, 16);
    STATIC_REQUIRE(r.size == 32);
    STATIC_REQUIRE(r.offset == 16);
    CHECK(r.stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT);
}

TEST_CASE("check_push_limit throws only when offset+size exceeds the device limit", "[vulkan]") {
    CHECK_NOTHROW(vkx::check_push_limit<Block32>(128));
    CHECK_NOTHROW(vkx::check_push_limit<Block32>(32));
    CHECK_THROWS_AS(vkx::check_push_limit<Block32>(31), std::runtime_error);
    CHECK_THROWS_AS(vkx::check_push_limit<Block32>(128, 112), std::runtime_error);  // 112 + 32 > 128
}
