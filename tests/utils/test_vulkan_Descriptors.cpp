// utils/vulkan/Descriptors.h: the pure parts (binding list, pool-size list, write struct).
#include "utils/vulkan/Descriptors.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

TEST_CASE("sampler_layout_bindings uses the given binding numbers and stage", "[vulkan]") {
    const uint32_t idx[3] = {0, 2, 5};
    const auto     b      = vkx::sampler_layout_bindings(idx, VK_SHADER_STAGE_FRAGMENT_BIT);
    REQUIRE(b.size() == 3);
    CHECK(b[0].binding == 0);
    CHECK(b[1].binding == 2);
    CHECK(b[2].binding == 5);
    for (const auto& x : b) {
        CHECK(x.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        CHECK(x.descriptorCount == 1);
        CHECK(x.stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT);
    }
}

TEST_CASE("pool_size_list omits zero counts and keeps a fixed order", "[vulkan]") {
    const auto only = vkx::pool_size_list({.maxSets = 2, .combinedImageSamplers = 8});
    REQUIRE(only.size() == 1);
    CHECK(only[0].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(only[0].descriptorCount == 8);

    const auto all =
        vkx::pool_size_list({.maxSets = 4, .combinedImageSamplers = 1, .uniformBuffers = 2, .storageBuffers = 3});
    REQUIRE(all.size() == 3);
    CHECK(all[0].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(all[1].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(all[1].descriptorCount == 2);
    CHECK(all[2].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(all[2].descriptorCount == 3);

    CHECK(vkx::pool_size_list({.maxSets = 1}).empty());
}

TEST_CASE("sampler_write targets one binding with the caller's image info", "[vulkan]") {
    const VkDescriptorImageInfo info{};
    const auto                  w = vkx::sampler_write(reinterpret_cast<VkDescriptorSet>(uintptr_t{0x20}), 3, &info);
    CHECK(w.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    CHECK(w.dstSet == reinterpret_cast<VkDescriptorSet>(uintptr_t{0x20}));
    CHECK(w.dstBinding == 3);
    CHECK(w.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(w.descriptorCount == 1);
    CHECK(w.pImageInfo == &info);
}
