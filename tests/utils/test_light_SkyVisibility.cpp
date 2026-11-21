// utils/light/SkyVisibility.h: how much sky a point under a roof can see (tasks/todo.md "Lighting lab").
// lighting.frag has a GLSL copy of roof_sky_visibility; these cases are the behaviour both must have.
#include "utils/light/SkyVisibility.h"
#include "utils/units/Units.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace ul = swish::util::light;
namespace uu = swish::util::units;
using Catch::Approx;

namespace {
// A 400 m tunnel along Z, 12 m wide and 6 m high, sky fading over the first 20 m.
ul::RoofVolume tunnel() {
    return {{uu::m(-6.0f), 0.0f, uu::m(-600.0f)}, {uu::m(6.0f), uu::m(6.0f), uu::m(-200.0f)}, uu::m(20.0f), 2, 0.02f};
}
// A deck `width` metres wide (along Z), open along Z, 12 m fade.
ul::RoofVolume deck(float width) {
    return {{uu::m(-18.0f), 0.0f, uu::m(-width)}, {uu::m(18.0f), uu::m(5.0f), 0.0f}, uu::m(12.0f), 2, 0.03f};
}
}  // namespace

TEST_CASE("Lab: open sky outside a roof volume", "[lighting][lab][sky_visibility]") {
    const ul::RoofVolume t = tunnel();
    CHECK(ul::roof_sky_visibility(t, {0.0f, uu::m(1.0f), uu::m(-100.0f)}) == 1.0f);          // before the mouth
    CHECK(ul::roof_sky_visibility(t, {uu::m(20.0f), uu::m(1.0f), uu::m(-400.0f)}) == 1.0f);  // beside the tunnel
    CHECK(ul::roof_sky_visibility(t, {0.0f, uu::m(9.0f), uu::m(-400.0f)}) == 1.0f);          // on top of the roof
}

TEST_CASE("Lab: the sky fades in from a tunnel mouth and is gone deep inside", "[lighting][lab][sky_visibility]") {
    const ul::RoofVolume t  = tunnel();
    const auto           at = [&](float zM) { return ul::roof_sky_visibility(t, {0.0f, uu::m(1.0f), uu::m(zM)}); };
    CHECK(at(-200.0f) == Approx(1.0f));                // exactly at the mouth
    CHECK(at(-210.0f) == Approx(0.51f).margin(0.01));  // halfway through the 20 m fade (smoothstep(0.5) = 0.5)
    CHECK(at(-220.0f) == Approx(0.02f));               // fade complete: the floor
    CHECK(at(-400.0f) == Approx(0.02f));               // the middle
    CHECK(at(-590.0f) == Approx(at(-210.0f)));         // symmetric at the exit
    CHECK(at(-205.0f) > at(-215.0f));                  // monotonic going in
}

TEST_CASE("Lab: a wider overpass is darker underneath", "[lighting][lab][sky_visibility]") {
    const auto mid = [](float width) {
        return ul::roof_sky_visibility(deck(width), {0.0f, uu::m(1.0f), uu::m(-width * 0.5f)});
    };
    CHECK(mid(10.0f) > mid(20.0f));
    CHECK(mid(20.0f) > mid(40.0f));
    CHECK(mid(40.0f) == Approx(0.03f));  // 20 m from each edge, past the 12 m fade
    CHECK(mid(80.0f) == Approx(0.03f));
    CHECK(mid(10.0f) > 0.5f);  // a 10 m deck only dims the sky
}

TEST_CASE("Lab: several volumes take the darkest; a volume open along X works too", "[lighting][lab][sky_visibility]") {
    const std::array<ul::RoofVolume, 2> both{tunnel(), deck(80.0f)};
    CHECK(ul::sky_visibility(both, {0.0f, uu::m(1.0f), uu::m(-40.0f)}) == Approx(0.03f));  // under the deck only
    CHECK(ul::sky_visibility(both, {0.0f, uu::m(1.0f), uu::m(-150.0f)}) == 1.0f);          // between them
    CHECK(ul::sky_visibility(std::span<const ul::RoofVolume>{}, {0.0f, 0.0f, 0.0f}) == 1.0f);

    ul::RoofVolume alongX = tunnel();
    std::swap(alongX.min.x, alongX.min.z);
    std::swap(alongX.max.x, alongX.max.z);
    alongX.openAxis = 0;
    CHECK(ul::roof_sky_visibility(alongX, {uu::m(-400.0f), uu::m(1.0f), 0.0f}) == Approx(0.02f));
    CHECK(ul::roof_sky_visibility(alongX, {uu::m(-205.0f), uu::m(1.0f), 0.0f}) > 0.5f);
}

TEST_CASE("Lab: closed rooms, canopies and one-ended garages", "[lighting][lab][sky_visibility]") {
    ul::RoofVolume room = tunnel();
    room.openAxis       = -1;
    room.floor          = 0.0f;
    CHECK(ul::roof_sky_visibility(room, {0.0f, uu::m(1.0f), uu::m(-201.0f)}) == 0.0f);  // dark right up to the wall
    CHECK(ul::roof_sky_visibility(room, {0.0f, uu::m(1.0f), uu::m(-100.0f)}) == 1.0f);  // outside is unaffected

    // A canopy is open on all four sides: the middle is darkest, every edge is bright.
    ul::RoofVolume canopy{
        {uu::m(-8.0f), 0.0f, uu::m(-6.0f)}, {uu::m(8.0f), uu::m(5.0f), uu::m(6.0f)}, uu::m(4.0f), 3, 0.2f};
    const auto at = [&](float x, float z) {
        return ul::roof_sky_visibility(canopy, {uu::m(x), uu::m(1.0f), uu::m(z)});
    };
    CHECK(at(0.0f, 0.0f) == Approx(0.2f));
    CHECK(at(7.9f, 0.0f) > 0.9f);
    CHECK(at(0.0f, -5.9f) > 0.9f);
    CHECK(at(0.0f, 0.0f) < at(6.0f, 0.0f));

    // A garage open only at its -Z end: bright at the entrance, dark at the closed far wall.
    ul::RoofVolume garage = tunnel();
    garage.openAxis       = 4;
    CHECK(ul::roof_sky_visibility(garage, {0.0f, uu::m(1.0f), uu::m(-599.0f)}) > 0.9f);  // min.z is the open end
    CHECK(ul::roof_sky_visibility(garage, {0.0f, uu::m(1.0f), uu::m(-201.0f)}) == Approx(0.02f));
}
