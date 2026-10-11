#include "renderer/Passes/PassDeclarationValidator.h"

#include <algorithm>
#include <array>

namespace swish {

const char* img_name(Img id) {
    switch (id) {
        case Img::Hdr:
            return "Hdr";
        case Img::HdrDepth:
            return "HdrDepth";
        case Img::GbAlbedo:
            return "GbAlbedo";
        case Img::GbNormal:
            return "GbNormal";
        case Img::GbMaterial:
            return "GbMaterial";
        case Img::GbClearcoat:
            return "GbClearcoat";
        case Img::Ao:
            return "Ao";
        case Img::AoBlur:
            return "AoBlur";
        case Img::BloomExtract:
            return "BloomExtract";
        case Img::BloomBlurH:
            return "BloomBlurH";
        case Img::BloomBlurV:
            return "BloomBlurV";
        case Img::Ssr:
            return "Ssr";
        case Img::Godrays:
            return "Godrays";
        case Img::Lum:
            return "Lum";
        case Img::Velocity:
            return "Velocity";
        case Img::kCount:
            break;
    }
    return "?";
}

std::span<const Img> default_primed_images() {
    // Ao / AoBlur: primed white by PostProcessManager::primeAOTexture; release has no SSAO pass, so the composite
    // reads the primed image. Lum: auto-exposure history (written by HdrFinalize, read next frame by the CPU).
    static constexpr std::array<Img, 3> kPrimed{Img::Ao, Img::AoBlur, Img::Lum};
    return kPrimed;
}

std::vector<std::string> validate_pass_declarations(const std::vector<NamedDeclaration>& ordered,
                                                    std::span<const Img>                 primed) {
    std::array<bool, kImgCount> written{};
    for (Img p : primed)
        written[static_cast<std::size_t>(p)] = true;

    std::vector<std::string> warnings;
    for (const NamedDeclaration& nd : ordered) {
        std::array<bool, kImgCount> reported{};  // one warning per image per pass
        for (Img r : nd.decl.reads) {
            const auto i = static_cast<std::size_t>(r);
            if (!written[i] && !reported[i]) {
                warnings.push_back(nd.name + " reads " + img_name(r) + " before any pass writes it");
                reported[i] = true;
            }
        }
        for (Img w : nd.decl.writes)
            written[static_cast<std::size_t>(w)] = true;
    }
    return warnings;
}

}  // namespace swish
