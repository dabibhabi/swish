#pragma once

// ── Tags.h ───────────────────────────────────────────────────────────
// Purpose   : empty "tag" types that pick an overload at compile time (tag dispatch), so a closed set of
//             variants (a tunable's value kind, later an image aspect) is handled by one overload set
//             instead of an `if (kind == X)` ladder repeated in several files.
// Units     : none.
// Complexity: zero cost; tags are empty, trivially copyable, constexpr. No traits, no hierarchy, no SFINAE.
// Naming    : namespace `swish::tag` (a vocabulary, like Vec3). Type `<name>_t`, constant `<name>`.
//             `flag_t` (a bool) and `real_t` (a float). Not `float_t`: that is a <cmath> typedef.
// Example   : void put(tag::flag_t, bool v);  void put(tag::real_t, float v);
//             put(tag::flag, true);  put(tag::real, 0.5f);
//             A runtime enum then needs ONE switch that calls f(tag::flag, x) or f(tag::real, x)
//             (see debug/Tunables.h `visit`); every consumer is just overloads.
// See also  : docs/DESIGN_PATTERNS.md section 2, debug/Tunables.h
// Test      : tests/utils/test_meta_Tags.cpp

#include <type_traits>

namespace swish::tag {

// A tunable / value that is a boolean checkbox.
struct flag_t {};
// A tunable / value that is a float slider.
struct real_t {};

inline constexpr flag_t flag{};
inline constexpr real_t real{};

// True for the tag types above. Use it to constrain a generic helper that must only receive a tag.
template <class T>
inline constexpr bool is_tag_v =
    std::is_same_v<std::remove_cv_t<T>, flag_t> || std::is_same_v<std::remove_cv_t<T>, real_t>;

}  // namespace swish::tag
