#pragma once

// ── CapturePath.h ────────────────────────────────────────────────────
// Purpose   : the Vulkan-free half of frame capture (FrameCapture.h): timestamped file names as a pure function of
//             a time value + counter, and in-memory encoders for the image files (binary PPM for the 8-bit
//             swapchain, PFM for the float HDR scene colour, half -> float conversion). Header-only, unit tested.
// Naming    : <dir>/swish_YYYYMMDD_HHMMSS_mmm[_<counter>].<ppm|pfm>. The counter is appended only when > 0 (two
//             captures in the same millisecond); the LDR and HDR files of one request share a stem.
// PFM       : "PF\n<w> <h>\n-1.0\n" (negative scale = little endian), RGB float32, rows stored BOTTOM-to-TOP.
//             Alpha is dropped. Input rows are top-down like every other image in the renderer.
// PNG       : TODO(stub): PNG output needs a deflate encoder; the vendored include/ has only stb_image.h (reader).
//             PPM opens in Preview / GIMP / ffmpeg and avoids a new dependency. Add stb_image_write (or a
//             minimal stored-deflate PNG) when a real format is wanted.
// Units     : none. No Vulkan includes, no I/O (the writer lives in FrameCapture.cpp).
// Test      : tests/utils/test_renderer_CapturePath.cpp

#include "utils/types/StdTypes.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace swish::capture {

static_assert(std::endian::native == std::endian::little, "PFM output below assumes a little-endian host");

enum class FileKind { Ppm, Pfm };

inline const char* extension(FileKind k) {
    return k == FileKind::Ppm ? "ppm" : "pfm";
}

// Calendar time broken down (already shifted to the wanted time zone).
struct Stamp {
    i32 year   = 1970;
    i32 month  = 1;  // 1..12
    i32 day    = 1;  // 1..31
    i32 hour   = 0;
    i32 minute = 0;
    i32 second = 0;
    i32 milli  = 0;
};

// Unix epoch milliseconds (+ a time-zone offset in minutes east of UTC) -> calendar fields.
// Civil-from-days algorithm (H. Hinnant, public domain); exact for the whole i64 range we care about.
inline Stamp stamp_from_unix_ms(i64 unixMs, i32 tzOffsetMinutes = 0) {
    const i64     ms   = unixMs + static_cast<i64>(tzOffsetMinutes) * 60000;
    constexpr i64 kDay = 86400000;
    i64           days = ms / kDay;
    i64           rem  = ms % kDay;
    if (rem < 0) {  // floor division for pre-1970 values
        rem += kDay;
        --days;
    }
    const i64 z   = days + 719468;
    const i64 era = (z >= 0 ? z : z - 146096) / 146097;
    const i64 doe = z - era * 146097;
    const i64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const i64 y   = yoe + era * 400;
    const i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const i64 mp  = (5 * doy + 2) / 153;
    const i64 d   = doy - (153 * mp + 2) / 5 + 1;
    const i64 m   = mp < 10 ? mp + 3 : mp - 9;

    Stamp s;
    s.year   = static_cast<i32>(y + (m <= 2 ? 1 : 0));
    s.month  = static_cast<i32>(m);
    s.day    = static_cast<i32>(d);
    s.hour   = static_cast<i32>(rem / 3600000);
    s.minute = static_cast<i32>(rem / 60000 % 60);
    s.second = static_cast<i32>(rem / 1000 % 60);
    s.milli  = static_cast<i32>(rem % 1000);
    return s;
}

namespace detail {
inline void append_padded(std::string& out, i32 v, int width) {
    std::string t = std::to_string(v < 0 ? 0 : v);
    if (static_cast<int>(t.size()) < width)
        out.append(static_cast<std::size_t>(width) - t.size(), '0');
    out += t;
}
}  // namespace detail

// "swish_YYYYMMDD_HHMMSS_mmm[_counter].ext" (no directory).
inline std::string file_name(const Stamp& s, u32 counter, FileKind kind) {
    std::string n = "swish_";
    detail::append_padded(n, s.year, 4);
    detail::append_padded(n, s.month, 2);
    detail::append_padded(n, s.day, 2);
    n += '_';
    detail::append_padded(n, s.hour, 2);
    detail::append_padded(n, s.minute, 2);
    detail::append_padded(n, s.second, 2);
    n += '_';
    detail::append_padded(n, s.milli, 3);
    if (counter > 0) {
        n += '_';
        n += std::to_string(counter);
    }
    n += '.';
    n += extension(kind);
    return n;
}

// dir + '/' + file_name (a trailing '/' on dir is not doubled; an empty dir gives just the name).
inline std::string full_path(const std::string& dir, const Stamp& s, u32 counter, FileKind kind) {
    const std::string n = file_name(s, counter, kind);
    if (dir.empty())
        return n;
    return dir.back() == '/' ? dir + n : dir + "/" + n;
}

// ── encoders ──────────────────────────────────────────────────────────

enum class PixelOrder { Rgba, Bgra };

// Tightly packed 8-bit 4-channel pixels (top-down) -> binary PPM (P6). Alpha dropped, channels reordered to RGB.
inline std::vector<u8> encode_ppm(const u8* px, u32 w, u32 h, PixelOrder order) {
    const std::string header = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    std::vector<u8>   out(header.begin(), header.end());
    const std::size_t n = static_cast<std::size_t>(w) * h;
    const std::size_t o = out.size();
    out.resize(o + n * 3);
    const int r = order == PixelOrder::Rgba ? 0 : 2;
    const int b = order == PixelOrder::Rgba ? 2 : 0;
    for (std::size_t i = 0; i < n; ++i) {
        out[o + i * 3 + 0] = px[i * 4 + static_cast<std::size_t>(r)];
        out[o + i * 3 + 1] = px[i * 4 + 1];
        out[o + i * 3 + 2] = px[i * 4 + static_cast<std::size_t>(b)];
    }
    return out;
}

// float pixels (top-down, `channels` = 3 or 4 interleaved) -> PFM RGB float32, rows flipped to bottom-to-top.
inline std::vector<u8> encode_pfm(const f32* px, u32 w, u32 h, u32 channels) {
    const std::string header = "PF\n" + std::to_string(w) + " " + std::to_string(h) + "\n-1.0\n";
    std::vector<u8>   out(header.begin(), header.end());
    const std::size_t o = out.size();
    out.resize(o + static_cast<std::size_t>(w) * h * 3 * sizeof(f32));
    std::size_t dst = o;
    for (u32 row = 0; row < h; ++row) {
        const f32* src = px + static_cast<std::size_t>(h - 1 - row) * w * channels;
        for (u32 x = 0; x < w; ++x) {
            std::memcpy(&out[dst], src + static_cast<std::size_t>(x) * channels, 3 * sizeof(f32));
            dst += 3 * sizeof(f32);
        }
    }
    return out;
}

// IEEE binary16 -> binary32 (handles zero, subnormals, inf, NaN). Used for the R16G16B16A16F HDR image.
inline f32 half_to_float(u16 h) {
    const u32 sign = static_cast<u32>(h & 0x8000u) << 16;
    u32       exp  = (h >> 10) & 0x1Fu;
    u32       man  = h & 0x3FFu;
    u32       bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;  // +-0
        } else {          // subnormal half -> normal float: renormalise
            exp = 127 - 15 + 1;
            while ((man & 0x400u) == 0) {
                man <<= 1;
                --exp;
            }
            man &= 0x3FFu;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (man << 13);  // inf / NaN
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (man << 13);
    }
    f32 f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// Tightly packed RGBA16F texels (u16 halves, 4 per pixel) -> float RGBA.
inline std::vector<f32> rgba16f_to_float(const u16* halves, std::size_t pixelCount) {
    std::vector<f32> out(pixelCount * 4);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = half_to_float(halves[i]);
    return out;
}

// Linear [0,1] -> 8-bit sRGB (IEC 61966-2-1 transfer, rounded, clamped). NaN maps to 0.
inline u8 linear_to_srgb8(f32 v) {
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 255;
    const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(e * 255.0f + 0.5f);
}

// The swapchain prefers R16G16B16A16_SFLOAT with extended-sRGB-linear colour space (display does the encoding), so
// an LDR dump of it must tone-clamp and sRGB-encode itself: RGBA16F linear -> RGBA8 sRGB (alpha forced to 255).
inline std::vector<u8> rgba16f_linear_to_rgba8_srgb(const u16* halves, std::size_t pixelCount) {
    std::vector<u8> out(pixelCount * 4);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        for (std::size_t c = 0; c < 3; ++c)
            out[i * 4 + c] = linear_to_srgb8(half_to_float(halves[i * 4 + c]));
        out[i * 4 + 3] = 255;
    }
    return out;
}

}  // namespace swish::capture
