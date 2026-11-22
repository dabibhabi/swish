#pragma once

// Car looks (docs/car-showcase.md): the numbers behind the showcase panel. Pure functions, no GPU.
//   paint_look        a paint finish + colour + surface state -> the material values for the body paint
//   car_material_fix  a sensible metalness / roughness for a car material, from its name
// Debug-only in effect: the App applies paint looks through the material overrides, and CarEntity applies the
// fixes only when asked. Test: tests/test_car_looks.cpp.

#include "scene/SceneTypes.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace swish {

enum class PaintFinish : int { Asset = 0, SolidGloss, Metallic, Pearl, Satin, Matte, Count };
enum class PaintColour : int { Asset = 0, Black, White, Silver, Red, DeepBlue, Yellow, Count };
enum class CarSurface : int { Clean = 0, Wet, Dusty, Count };

[[nodiscard]] inline const char* paint_finish_name(int i) {
    static constexpr const char* k[] = {"As the asset", "Solid gloss", "Metallic", "Pearl", "Satin", "Matte"};
    return k[std::clamp(i, 0, static_cast<int>(PaintFinish::Count) - 1)];
}
[[nodiscard]] inline const char* paint_colour_name(int i) {
    static constexpr const char* k[] = {"As the asset", "Black", "White", "Silver", "Red", "Deep blue", "Yellow"};
    return k[std::clamp(i, 0, static_cast<int>(PaintColour::Count) - 1)];
}

// What the asset's own paint looks like, so "as the asset" has something to start from.
struct PaintBase {
    float metalness    = 0.0f;
    float clearcoat    = kDefaultClearcoatStrength;
    float flakeDensity = 0.0f;
};

// The body-paint material for a finish, a colour and a surface state. `enabled` is false when all three are
// "as the asset / clean", so the override can be switched off and the asset shows untouched.
//   finish  metalness and roughness of the base coat, the clear coat on top, flakes in it
//   colour  the base colour (linear albedo); "as the asset" keeps the texture's colour (white tint)
//   dusty   rougher, a duller clear coat, a tint of dust
[[nodiscard]] inline MaterialOverride paint_look(PaintFinish finish, PaintColour colour, CarSurface surface,
                                                 const PaintBase& base = {}) {
    MaterialOverride o;
    o.enabled      = finish != PaintFinish::Asset || colour != PaintColour::Asset || surface == CarSurface::Dusty;
    o.metalness    = base.metalness;
    o.roughnessMul = 1.0f;
    o.clearcoat    = base.clearcoat;
    o.flakeDensity = base.flakeDensity;
    o.color        = Vec3(1.0f);
    switch (finish) {
        case PaintFinish::SolidGloss:
            o.metalness = 0.0f, o.roughnessMul = 1.0f, o.clearcoat = 0.6f, o.flakeDensity = 0.0f;
            break;
        case PaintFinish::Metallic:
            o.metalness = 0.7f, o.roughnessMul = 1.8f, o.clearcoat = 0.6f, o.flakeDensity = 0.45f;
            break;
        case PaintFinish::Pearl:
            o.metalness = 0.25f, o.roughnessMul = 1.4f, o.clearcoat = 0.6f, o.flakeDensity = 0.2f;
            break;
        case PaintFinish::Satin:
            o.metalness = 0.0f, o.roughnessMul = 3.0f, o.clearcoat = 0.15f, o.flakeDensity = 0.0f;
            break;
        case PaintFinish::Matte:
            o.metalness = 0.0f, o.roughnessMul = 5.5f, o.clearcoat = 0.0f, o.flakeDensity = 0.0f;
            break;
        default:
            break;
    }
    switch (colour) {
        case PaintColour::Black:
            o.color = Vec3(0.02f);
            break;
        case PaintColour::White:
            o.color = Vec3(0.85f);
            break;
        case PaintColour::Silver:
            o.color = Vec3(0.55f);
            break;
        case PaintColour::Red:
            o.color = Vec3(0.55f, 0.02f, 0.02f);
            break;
        case PaintColour::DeepBlue:
            o.color = Vec3(0.02f, 0.06f, 0.30f);
            break;
        case PaintColour::Yellow:
            o.color = Vec3(0.85f, 0.60f, 0.02f);
            break;
        default:
            break;
    }
    if (surface == CarSurface::Dusty) {
        o.roughnessMul *= 2.5f;
        o.clearcoat *= 0.3f;
        o.flakeDensity *= 0.3f;
        o.color = glm::mix(o.color, Vec3(0.45f, 0.40f, 0.33f), 0.3f);  // a film of dust
    }
    return o;
}

// A corrected metalness and roughness for a car material, by name. Car assets often carry exporter noise
// (the rigged Porsche's tyre is 62% metallic). `metalness` < 0 or `roughness` < 0 means "keep the asset's".
struct MaterialFix {
    float metalness = -1.0f;
    float roughness = -1.0f;
};
[[nodiscard]] inline MaterialFix car_material_fix(std::string_view materialName) {
    std::string n(materialName);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto has = [&](std::string_view key) { return n.find(key) != std::string::npos; };
    // Body paint is the asset's (and the looks panel's) business.
    if (has("paint") || has("_ext1")) return {};
    if (has("tyre") || has("tire") || has("rubber")) return {0.0f, 0.85f};
    if (has("glass") || has("window")) return {0.0f, 0.05f};
    if (has("chrome") || has("mirror")) return {1.0f, 0.06f};
    if (has("rotor") || has("disc")) return {1.0f, 0.45f};
    if (has("wheel") || has("rim")) return {0.9f, 0.35f};
    // Cabin: leather, fabric, plastic, stitching. Never metal; the asset's roughness is kept.
    if (n.rfind("int", 0) == 0 || has("_int") || has("interior") || has("leather") || has("carpet") || has("fabric") ||
        has("plastic") || has("seat") || has("dash") || has("costuras") || has("skepaker") || n.rfind("mat0", 0) == 0)
        return {0.0f, -1.0f};
    return {};
}

}  // namespace swish
