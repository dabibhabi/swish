#include "renderer/FrameCapture/CapturePath.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace cap = swish::capture;
using swish::f32;
using swish::u16;
using swish::u8;

TEST_CASE("stamp_from_unix_ms decodes known instants", "[capture]") {
    auto s = cap::stamp_from_unix_ms(0);
    REQUIRE(s.year == 1970);
    REQUIRE(s.month == 1);
    REQUIRE(s.day == 1);
    REQUIRE(s.hour == 0);
    REQUIRE(s.milli == 0);

    // 2026-10-10 12:34:56.789 UTC = 1791635696789 ms
    s = cap::stamp_from_unix_ms(1791635696789LL);
    REQUIRE(s.year == 2026);
    REQUIRE(s.month == 10);
    REQUIRE(s.day == 10);
    REQUIRE(s.hour == 12);
    REQUIRE(s.minute == 34);
    REQUIRE(s.second == 56);
    REQUIRE(s.milli == 789);
}

TEST_CASE("stamp_from_unix_ms handles leap day and time-zone rollover", "[capture]") {
    // 2024-02-29 23:59:59.999 UTC = 1709251199999
    auto s = cap::stamp_from_unix_ms(1709251199999LL);
    REQUIRE(s.year == 2024);
    REQUIRE(s.month == 2);
    REQUIRE(s.day == 29);
    REQUIRE(s.hour == 23);
    // +60 minutes crosses into March 1.
    s = cap::stamp_from_unix_ms(1709251199999LL, 60);
    REQUIRE(s.month == 3);
    REQUIRE(s.day == 1);
    REQUIRE(s.hour == 0);
    REQUIRE(s.minute == 59);
    // Negative offset from the epoch lands on the previous day (floor division).
    s = cap::stamp_from_unix_ms(0, -300);
    REQUIRE(s.year == 1969);
    REQUIRE(s.month == 12);
    REQUIRE(s.day == 31);
    REQUIRE(s.hour == 19);
}

TEST_CASE("file_name zero-pads and appends the counter only when non-zero", "[capture]") {
    cap::Stamp s{2026, 1, 2, 3, 4, 5, 6};
    REQUIRE(cap::file_name(s, 0, cap::FileKind::Ppm) == "swish_20260102_030405_006.ppm");
    REQUIRE(cap::file_name(s, 0, cap::FileKind::Pfm) == "swish_20260102_030405_006.pfm");
    REQUIRE(cap::file_name(s, 3, cap::FileKind::Ppm) == "swish_20260102_030405_006_3.ppm");
}

TEST_CASE("full_path joins the directory once", "[capture]") {
    cap::Stamp s{2026, 10, 10, 12, 34, 56, 789};
    REQUIRE(cap::full_path("build-debug/captures", s, 0, cap::FileKind::Ppm) ==
            "build-debug/captures/swish_20261010_123456_789.ppm");
    REQUIRE(cap::full_path("build-debug/captures/", s, 0, cap::FileKind::Ppm) ==
            "build-debug/captures/swish_20261010_123456_789.ppm");
    REQUIRE(cap::full_path("", s, 0, cap::FileKind::Pfm) == "swish_20261010_123456_789.pfm");
}

TEST_CASE("encode_ppm writes a P6 header and reorders BGRA to RGB", "[capture]") {
    const u8          bgra[] = {3, 2, 1, 255, 30, 20, 10, 255};  // two pixels: (R1,G2,B3) and (R10,G20,B30)
    auto              out    = cap::encode_ppm(bgra, 2, 1, cap::PixelOrder::Bgra);
    const std::string header = "P6\n2 1\n255\n";
    REQUIRE(out.size() == header.size() + 6);
    REQUIRE(std::string(out.begin(), out.begin() + static_cast<long>(header.size())) == header);
    const u8 expect[] = {1, 2, 3, 10, 20, 30};
    REQUIRE(std::memcmp(out.data() + header.size(), expect, 6) == 0);

    const u8 rgba[] = {1, 2, 3, 255};
    out             = cap::encode_ppm(rgba, 1, 1, cap::PixelOrder::Rgba);
    REQUIRE(out[out.size() - 3] == 1);
    REQUIRE(out[out.size() - 1] == 3);
}

TEST_CASE("encode_pfm writes the header, drops alpha and flips rows bottom-to-top", "[capture]") {
    // 1 wide, 2 tall RGBA float: top row (1,2,3,.9), bottom row (4,5,6,.9)
    const f32         px[]   = {1, 2, 3, 0.9f, 4, 5, 6, 0.9f};
    auto              out    = cap::encode_pfm(px, 1, 2, 4);
    const std::string header = "PF\n1 2\n-1.0\n";
    REQUIRE(out.size() == header.size() + 6 * sizeof(f32));
    REQUIRE(std::string(out.begin(), out.begin() + static_cast<long>(header.size())) == header);
    f32 got[6];
    std::memcpy(got, out.data() + header.size(), sizeof got);
    const f32 expect[6] = {4, 5, 6, 1, 2, 3};  // bottom row first
    for (int i = 0; i < 6; ++i) REQUIRE(got[i] == expect[i]);
}

TEST_CASE("encode_pfm accepts 3-channel input", "[capture]") {
    const f32 px[] = {7, 8, 9};
    auto      out  = cap::encode_pfm(px, 1, 1, 3);
    f32       got[3];
    std::memcpy(got, out.data() + out.size() - sizeof got, sizeof got);
    REQUIRE(got[0] == 7);
    REQUIRE(got[2] == 9);
}

TEST_CASE("half_to_float matches IEEE binary16 landmarks", "[capture]") {
    REQUIRE(cap::half_to_float(0x0000) == 0.0f);
    REQUIRE(cap::half_to_float(0x3C00) == 1.0f);
    REQUIRE(cap::half_to_float(0xC000) == -2.0f);
    REQUIRE(cap::half_to_float(0x3800) == 0.5f);
    REQUIRE(cap::half_to_float(0x7BFF) == 65504.0f);
    REQUIRE(std::isinf(cap::half_to_float(0x7C00)));
    REQUIRE(std::isinf(cap::half_to_float(0xFC00)));
    REQUIRE(cap::half_to_float(0xFC00) < 0.0f);
    REQUIRE(std::isnan(cap::half_to_float(0x7E00)));
    REQUIRE(std::signbit(cap::half_to_float(0x8000)));  // -0
    // Smallest subnormal half = 2^-24; largest subnormal = 1023 * 2^-24.
    REQUIRE(cap::half_to_float(0x0001) == std::ldexp(1.0f, -24));
    REQUIRE(cap::half_to_float(0x03FF) == std::ldexp(1023.0f, -24));
}

TEST_CASE("linear_to_srgb8 clamps and encodes", "[capture]") {
    REQUIRE(cap::linear_to_srgb8(-1.0f) == 0);
    REQUIRE(cap::linear_to_srgb8(0.0f) == 0);
    REQUIRE(cap::linear_to_srgb8(std::nanf("")) == 0);
    REQUIRE(cap::linear_to_srgb8(1.0f) == 255);
    REQUIRE(cap::linear_to_srgb8(40.0f) == 255);  // HDR values clamp
    REQUIRE(cap::linear_to_srgb8(0.22f) == 129);  // 0.22 linear -> sRGB 0.5064 -> 129.6 (mid-grey is ~0.214)
    REQUIRE(cap::linear_to_srgb8(0.001f) == 3);   // linear toe: 0.001 * 12.92 * 255
}

TEST_CASE("rgba16f_linear_to_rgba8_srgb forces opaque alpha", "[capture]") {
    const u16 halves[] = {0x3C00, 0x0000, 0x3800, 0x0000};  // R=1, G=0, B=0.5, A=0
    auto      px       = cap::rgba16f_linear_to_rgba8_srgb(halves, 1);
    REQUIRE(px.size() == 4);
    REQUIRE(px[0] == 255);
    REQUIRE(px[1] == 0);
    REQUIRE(px[2] == 188);  // 0.5 linear -> sRGB 0.7354 -> 187.5 -> 188
    REQUIRE(px[3] == 255);
}

TEST_CASE("rgba16f_to_float converts every channel", "[capture]") {
    const u16 halves[] = {0x3C00, 0x4000, 0x4200, 0x3C00};  // 1, 2, 3, 1
    auto      f        = cap::rgba16f_to_float(halves, 1);
    REQUIRE(f.size() == 4);
    REQUIRE(f[0] == 1.0f);
    REQUIRE(f[1] == 2.0f);
    REQUIRE(f[2] == 3.0f);
}
