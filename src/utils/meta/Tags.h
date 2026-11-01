#pragma once


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
