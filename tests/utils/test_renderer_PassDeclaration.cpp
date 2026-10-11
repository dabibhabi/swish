// renderer/Passes: PassDeclaration builder + PassDeclarationValidator (read-before-write in list order, allowlist),
// on synthetic data. The declarations of the real passes are checked at start-up in debug builds (Renderer::init logs
// "[PassDeclaration] WARNING"); this file pins the validator rules and a mirror of the shipped chain.
#include "renderer/Passes/PassDeclarationValidator.h"
#include "renderer/Passes/PassRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace swish;

namespace {

NamedDeclaration nd(const char* name, std::initializer_list<Img> reads, std::initializer_list<Img> writes) {
    NamedDeclaration n;
    n.name = name;
    n.decl.read(reads).write(writes);
    return n;
}

}  // namespace

TEST_CASE("PassDeclaration builder collects reads and writes; default pass declares nothing", "[renderer][declare]") {
    PassDeclaration d;
    CHECK(d.empty());
    d.read({Img::Hdr, Img::Ssr}).write({Img::Hdr});
    CHECK_FALSE(d.empty());
    REQUIRE(d.reads.size() == 2);
    CHECK(d.reads[1] == Img::Ssr);
    REQUIRE(d.writes.size() == 1);

    struct Silent final : IRenderPass {
        const char* name() const override { return "Silent"; }
        void        init(const PassInitCtx&) override {}
        void        record(const FrameCtx&) override {}
        void        cleanup(VkDevice) override {}
    };
    PassDeclaration s;
    Silent{}.declare(s);
    CHECK(s.empty());
}

TEST_CASE("validator finds a read before any write", "[renderer][declare]") {
    const std::vector<NamedDeclaration> list = {
        nd("Composite", {Img::Hdr}, {}),
        nd("Lighting", {}, {Img::Hdr}),
    };
    const auto w = validate_pass_declarations(list, {});
    REQUIRE(w.size() == 1);
    CHECK(w[0] == "Composite reads Hdr before any pass writes it");
}

TEST_CASE("validator accepts write-then-read and in-place read+write after a producer", "[renderer][declare]") {
    const std::vector<NamedDeclaration> list = {
        nd("Lighting", {}, {Img::Hdr}),
        nd("Dof", {Img::Hdr}, {Img::Hdr}),
        nd("Composite", {Img::Hdr}, {}),
    };
    CHECK(validate_pass_declarations(list, {}).empty());
}

TEST_CASE("a pass's own write does not satisfy its own read", "[renderer][declare]") {
    const std::vector<NamedDeclaration> list = {nd("Taa", {Img::Hdr}, {Img::Hdr})};
    CHECK(validate_pass_declarations(list, {}).size() == 1);
}

TEST_CASE("validator warns once per image per pass", "[renderer][declare]") {
    const std::vector<NamedDeclaration> list = {nd("P", {Img::Ssr, Img::Ssr, Img::Godrays}, {})};
    CHECK(validate_pass_declarations(list, {}).size() == 2);
}

TEST_CASE("primed images are exempt; the default allowlist covers AO and the luminance history",
          "[renderer][declare]") {
    const std::vector<NamedDeclaration> list = {nd("Composite", {Img::AoBlur, Img::Lum}, {})};
    CHECK(validate_pass_declarations(list).empty());          // default allowlist
    CHECK(validate_pass_declarations(list, {}).size() == 2);  // no allowlist
    const Img  only[] = {Img::AoBlur};
    const auto w      = validate_pass_declarations(list, only);  // custom allowlist
    REQUIRE(w.size() == 1);
    CHECK(w[0].find("Lum") != std::string::npos);
}

TEST_CASE("the default allowlist does not hide ordinary images", "[renderer][declare]") {
    for (Img id : {Img::Hdr, Img::Ssr, Img::Godrays, Img::Velocity, Img::GbAlbedo, Img::BloomBlurV}) {
        const std::vector<NamedDeclaration> list = {nd("X", {id}, {})};
        CHECK(validate_pass_declarations(list).size() == 1);
    }
}

TEST_CASE("img_name covers every image", "[renderer][declare]") {
    for (std::size_t i = 0; i < kImgCount; ++i)
        CHECK(std::string(img_name(static_cast<Img>(i))) != "?");
}

TEST_CASE("the shipped release chain is warning-free; a reorder is caught", "[renderer][declare]") {
    // Mirror of the declarations in the passes, in PassList.def release order (see each pass's declare()).
    const std::vector<NamedDeclaration> chain = {
        nd("Geometry", {}, {Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat, Img::HdrDepth}),
        nd("Lighting", {Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat, Img::HdrDepth}, {Img::Hdr}),
        nd("Ssr", {Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat, Img::HdrDepth, Img::Hdr},
           {Img::Ssr}),
        nd("GodRays", {Img::HdrDepth, Img::Hdr}, {Img::Godrays}),
        nd("SpraySim", {}, {}),
        nd("Rain", {Img::Hdr, Img::HdrDepth}, {Img::Hdr}),
        nd("SprayDraw", {Img::Hdr, Img::HdrDepth}, {Img::Hdr}),
        nd("Glass", {Img::Hdr, Img::HdrDepth}, {Img::Hdr}),
        nd("WindshieldRain", {Img::Hdr, Img::HdrDepth}, {Img::Hdr}),
        nd("HdrFinalize", {Img::Hdr}, {}),
        nd("BloomExtract", {Img::Hdr}, {Img::BloomExtract}),
        nd("BloomBlurH", {Img::BloomExtract}, {Img::BloomBlurH}),
        nd("BloomBlurV", {Img::BloomBlurH}, {Img::BloomBlurV}),
        nd("Composite", {Img::Hdr, Img::AoBlur, Img::BloomBlurV, Img::Ssr, Img::Godrays}, {}),
    };
    CHECK(validate_pass_declarations(chain).empty());

    // Names match the pinned pass list (PassList.def release expansion), same order.
    static const char* const kRelease[] = {
#define PRODUCER(Name) #Name,
#define PASS(Name, Factory) #Name,
#define PASS_DEBUG(Name, Factory)
#include "renderer/Passes/PassList.def"
#undef PRODUCER
#undef PASS
#undef PASS_DEBUG
    };
    REQUIRE(std::size(kRelease) == chain.size());
    for (std::size_t i = 0; i < chain.size(); ++i)
        CHECK(chain[i].name == kRelease[i]);

    // Debug list (PASS_DEBUG entries included): SSAO/AO blur/velocity after lighting, DoF after god-rays, TAA after
    // the windshield, luminance pyramid in HdrFinalize, the overlay last (declares nothing).
    const std::vector<NamedDeclaration> debugChain = {
        chain[0],
        chain[1],
        nd("Ssao", {Img::HdrDepth}, {Img::Ao}),
        nd("AoBlur", {Img::Ao}, {Img::AoBlur}),
        nd("Velocity", {Img::HdrDepth}, {Img::Velocity}),
        chain[2],
        chain[3],
        nd("Dof", {Img::Hdr, Img::HdrDepth}, {Img::Hdr}),
        chain[4],
        chain[5],
        chain[6],
        chain[7],
        chain[8],
        nd("Taa", {Img::Hdr, Img::HdrDepth, Img::Velocity}, {Img::Hdr}),
        nd("HdrFinalize", {Img::Hdr}, {Img::Lum}),
        chain[10],
        chain[11],
        chain[12],
        chain[13],
        nd("DebugOverlay", {}, {}),
    };
    CHECK(validate_pass_declarations(debugChain).empty());
    // Taa before the velocity pass would sample a never-written velocity image.
    auto badDebug = debugChain;
    std::swap(badDebug[4], badDebug[13]);
    CHECK_FALSE(validate_pass_declarations(badDebug).empty());

    // Moving the composite before the SSR pass reads a never-written SSR image.
    auto swapped = chain;
    std::swap(swapped[2], swapped[13]);
    CHECK_FALSE(validate_pass_declarations(swapped).empty());
}
