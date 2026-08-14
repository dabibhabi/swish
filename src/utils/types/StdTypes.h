#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace swish {

// Fixed-width primitives
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

// Container / ownership aliases
template <typename T>
using vector = std::vector<T>;

template <typename T, usize N>
using array = std::array<T, N>;

template <typename T>
using unique_ptr = std::unique_ptr<T>;

using str = std::string;

}  // namespace swish
