// renderer/Passes: PassRegistry lifecycle order + name rule, ImageRegistry storage, PassList.def order (pinned to the
// pre-registry Renderer::recordCommandBuffer order), declared sampler inputs. No Vulkan call is made: the fake passes
// only record which hook ran.
#include "renderer/Passes/ImageRegistry.h"
#include "renderer/Passes/PassInputs.h"
#include "renderer/Passes/PassRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace swish;

namespace {

struct Log {
    std::vector<std::string> events;
};

class FakePass : public IRenderPass {
public:
    FakePass(const char* n, Log& l) : m_name(n), m_log(l) {}
    const char* name() const override { return m_name; }
    void        init(const PassInitCtx&) override { m_log.events.push_back(std::string("init:") + m_name); }
    void        onResize(const PassInitCtx&) override { m_log.events.push_back(std::string("resize:") + m_name); }
    void        beginFrame(const FrameUpdate&) override { m_log.events.push_back(std::string("begin:") + m_name); }
    void        update(const FrameUpdate&) override { m_log.events.push_back(std::string("update:") + m_name); }
    void        record(const FrameCtx&) override { m_log.events.push_back(std::string("record:") + m_name); }
    void        cleanup(VkDevice) override { m_log.events.push_back(std::string("cleanup:") + m_name); }

private:
    const char* m_name;
    Log&        m_log;
};

class OtherPass final : public FakePass {
public:
    using FakePass::FakePass;
};

// The hooks take context structs that hold references; these tests never call them through the registry's
// record_all/init_all (they would need a full context), so the lifecycle is checked through direct calls below.
}  // namespace

TEST_CASE("PassRegistry keeps insertion order and rejects a duplicate name", "[renderer]") {
    Log          log;
    PassRegistry r;
    r.add(std::make_unique<FakePass>("A", log));
    r.add(std::make_unique<OtherPass>("B", log));
    FakePass borrowed("C", log);
    r.add_borrowed(&borrowed);

    REQUIRE(r.size() == 3);
    const auto names = r.names();
    CHECK(names[0] == "A");
    CHECK(names[1] == "B");
    CHECK(names[2] == "C");

    CHECK_THROWS_AS(r.add(std::make_unique<FakePass>("B", log)), std::logic_error);
    CHECK_THROWS_AS(r.add_borrowed(&borrowed), std::logic_error);
    CHECK(r.size() == 3);
}

TEST_CASE("PassRegistry::find returns the first pass of the dynamic type", "[renderer]") {
    Log          log;
    PassRegistry r;
    r.add(std::make_unique<FakePass>("A", log));
    r.add(std::make_unique<OtherPass>("B", log));
    REQUIRE(r.find<OtherPass>() != nullptr);
    CHECK(std::strcmp(r.find<OtherPass>()->name(), "B") == 0);
    CHECK(std::strcmp(r.find<FakePass>()->name(), "A") == 0);
    struct Unrelated final : IRenderPass {
        const char* name() const override { return "U"; }
        void        init(const PassInitCtx&) override {}
        void        record(const FrameCtx&) override {}
        void        cleanup(VkDevice) override {}
    };
    CHECK(r.find<Unrelated>() == nullptr);
}

TEST_CASE("PassRegistry::cleanup_all runs in reverse list order and drops owned passes", "[renderer]") {
    Log          log;
    PassRegistry r;
    r.add(std::make_unique<FakePass>("A", log));
    r.add(std::make_unique<FakePass>("B", log));
    FakePass borrowed("C", log);
    r.add_borrowed(&borrowed);
    r.cleanup_all(VK_NULL_HANDLE);
    REQUIRE(log.events.size() == 3);
    CHECK(log.events[0] == "cleanup:C");
    CHECK(log.events[1] == "cleanup:B");
    CHECK(log.events[2] == "cleanup:A");
    CHECK(r.size() == 0);
}

TEST_CASE("ImageRegistry stores per-frame and shared images by name", "[renderer]") {
    ImageRegistry reg;
    CHECK_FALSE(reg.has(Img::Hdr));

    const auto img0 = reinterpret_cast<VkImage>(uintptr_t{0x10});
    const auto img1 = reinterpret_cast<VkImage>(uintptr_t{0x11});
    const auto v0   = reinterpret_cast<VkImageView>(uintptr_t{0x20});
    const auto v1   = reinterpret_cast<VkImageView>(uintptr_t{0x21});
    reg.set(Img::Hdr, 0, img0, v0);
    reg.set(Img::Hdr, 1, img1, v1);
    CHECK(reg.has(Img::Hdr));
    CHECK(reg.image(Img::Hdr, 0) == img0);
    CHECK(reg.image(Img::Hdr, 1) == img1);
    const auto views = reg.views(Img::Hdr);
    CHECK(views[0] == v0);
    CHECK(views[1] == v1);

    reg.set_shared(Img::Ssr, img0, v0);
    CHECK(reg.image(Img::Ssr, 0) == reg.image(Img::Ssr, 1));
    CHECK(reg.view(Img::Ssr, 1) == v0);

    // Re-registering (swapchain recreate) replaces the handles.
    reg.set(Img::Hdr, 0, img1, v1);
    CHECK(reg.image(Img::Hdr, 0) == img1);
}

TEST_CASE("PassList.def pins the frame order (release list)", "[renderer]") {
    // Same expansion PassList.cpp uses, but into names. Debug-only entries are dropped like a release build does.
    static const char* const kRelease[] = {
#define PRODUCER(Name) #Name,
#define PASS(Name, Factory) #Name,
#define PASS_DEBUG(Name, Factory)
#include "renderer/Passes/PassList.def"
#undef PRODUCER
#undef PASS
#undef PASS_DEBUG
    };
    static const char* const kAll[] = {
#define PRODUCER(Name) #Name,
#define PASS(Name, Factory) #Name,
#define PASS_DEBUG(Name, Factory) #Name,
#include "renderer/Passes/PassList.def"
#undef PRODUCER
#undef PASS
#undef PASS_DEBUG
    };

    // Renderer::recordCommandBuffer order before the registry existed (2026-09-29), release build.
    const std::vector<std::string> expectedRelease = {
        "Geometry", "Lighting",       "Ssr",         "GodRays",      "SpraySim",   "Rain",       "SprayDraw",
        "Glass",    "WindshieldRain", "HdrFinalize", "BloomExtract", "BloomBlurH", "BloomBlurV", "Composite"};
    REQUIRE(std::size(kRelease) == expectedRelease.size());
    for (size_t i = 0; i < expectedRelease.size(); ++i) CHECK(expectedRelease[i] == kRelease[i]);

    // Debug builds add SSAO + AO blur + velocity (after lighting), DoF (after god-rays), TAA (after the windshield)
    // and the overlay (last).
    const std::vector<std::string> expectedAll = {
        "Geometry",   "Lighting",   "Ssao",      "AoBlur",      "Velocity",       "Ssr", "GodRays",     "Dof",
        "SpraySim",   "Rain",       "SprayDraw", "Glass",       "WindshieldRain", "Taa", "HdrFinalize", "BloomExtract",
        "BloomBlurH", "BloomBlurV", "Composite", "DebugOverlay"};
    REQUIRE(std::size(kAll) == expectedAll.size());
    for (size_t i = 0; i < expectedAll.size(); ++i) CHECK(expectedAll[i] == kAll[i]);
}

TEST_CASE("declared sampler inputs are dense and use the bindings.glsl numbers", "[renderer]") {
    STATIC_REQUIRE(bindings_dense(kCompositeInputs));
    STATIC_REQUIRE(bindings_dense(kGBufferInputs));
    STATIC_REQUIRE(kCompositeInputs[0].binding == bind::kBindCompHdr);
    STATIC_REQUIRE(kCompositeInputs[4].binding == bind::kBindCompGodrays);
    STATIC_REQUIRE(kCompositeInputs[0].img == Img::Hdr);

    // Depth is sampled in the depth read-only layout; everything else in SHADER_READ_ONLY.
    for (const SamplerInput& in : kGBufferInputs) {
        if (in.img == Img::HdrDepth)
            CHECK(in.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        else
            CHECK(in.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    constexpr std::array<SamplerInput, 2> gappy{{{Img::Hdr, 0}, {Img::Ssr, 2}}};
    STATIC_REQUIRE_FALSE(bindings_dense(gappy));
    constexpr std::array<SamplerInput, 2> dup{{{Img::Hdr, 1}, {Img::Ssr, 1}}};
    STATIC_REQUIRE_FALSE(bindings_dense(dup));
    constexpr auto nums = binding_numbers(kCompositeInputs);
    STATIC_REQUIRE(nums[3] == 3);
}
