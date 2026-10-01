#pragma once

// ── EnumCycle.h ──────────────────────────────────────────────────────
// Purpose   : count and "next (wrapping)" helpers for enums used as cyclic presets (camera mode,
//             weather preset), so each enum stops hand-writing the same two functions.
// Units     : none. Requirement: an enumerator named `Count` that is one past the last real value,
//             and consecutive values starting at 0.
// Complexity: O(1), constexpr, no allocation.
// Example   : enum class Mode : uint8_t { A = 0, B, C, Count };
//             namespace um = swish::util::meta;
//             static_assert(um::enum_count<Mode>() == 3);
//             Mode m = um::next_enum(Mode::C);   // wraps to Mode::A
// See also  : scene/CameraMode.h, scene/WeatherPreset.h
// Test      : tests/utils/test_meta_EnumCycle.cpp

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace swish::util::meta {

// `Count == 0` is rejected: next_enum would take a modulo by zero.
template <typename E>
concept CountedEnum = std::is_enum_v<E> && requires { E::Count; } && (static_cast<long long>(E::Count) > 0);

// Number of real values (the `Count` sentinel).
template <CountedEnum E>
constexpr std::underlying_type_t<E> enum_count() noexcept {
    return static_cast<std::underlying_type_t<E>>(E::Count);
}

// Next value, wrapping from the last real value back to 0.
template <CountedEnum E>
constexpr E next_enum(E e) noexcept {
    using U      = std::underlying_type_t<E>;
    const auto i = static_cast<U>(e);
    const auto n = enum_count<E>();
    return static_cast<E>(static_cast<U>((i + 1u) % n));
}

}  // namespace swish::util::meta
