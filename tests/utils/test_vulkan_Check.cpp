// utils/vulkan/Check.h: VK_CHECK + vkResultString. Header-only; needs the Vulkan headers but not the loader.
#include "utils/vulkan/Check.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstring>
#include <string>

namespace vkx = swish::vkx;

TEST_CASE("vkResultString names the common codes and falls back for unknown ones", "[vulkan]") {
    CHECK(std::strcmp(vkx::vkResultString(VK_SUCCESS), "VK_SUCCESS") == 0);
    CHECK(std::strcmp(vkx::vkResultString(VK_ERROR_OUT_OF_DEVICE_MEMORY), "VK_ERROR_OUT_OF_DEVICE_MEMORY") == 0);
    CHECK(std::strcmp(vkx::vkResultString(VK_ERROR_OUT_OF_DATE_KHR), "VK_ERROR_OUT_OF_DATE_KHR") == 0);
    CHECK(std::strcmp(vkx::vkResultString(VK_SUBOPTIMAL_KHR), "VK_SUBOPTIMAL_KHR") == 0);
    CHECK(std::strcmp(vkx::vkResultString(static_cast<VkResult>(-999999)), "VK_UNKNOWN_ERROR") == 0);
}

TEST_CASE("VK_CHECK passes VK_SUCCESS and throws with file line and symbolic name otherwise", "[vulkan]") {
    REQUIRE_NOTHROW([] { VK_CHECK(VK_SUCCESS); }());
    try {
        VK_CHECK(VK_ERROR_DEVICE_LOST);
        FAIL("VK_CHECK did not throw");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        CHECK(msg.find("test_vulkan_Check.cpp") != std::string::npos);
        CHECK(msg.find("VK_ERROR_DEVICE_LOST") != std::string::npos);
    }
}

TEST_CASE("VK_CHECK evaluates its expression exactly once", "[vulkan]") {
    int  calls = 0;
    auto f     = [&]() {
        ++calls;
        return VK_SUCCESS;
    };
    VK_CHECK(f());
    CHECK(calls == 1);
}
