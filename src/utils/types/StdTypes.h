#pragma once

// ── StdTypes.h ───────────────────────────────────────────────────────
// Purpose   : short fixed-width scalar aliases (u32, f32, usize, ...) used across the codebase.
// Units     : none. Namespace is plain `swish::` (vocabulary types, see Types.h).
// Complexity: compile-time only.
// Example   : swish::u32 count = 3;  swish::f32 dt = 0.016f;
// See also  : Types.h (umbrella). The old swish::vector/array/unique_ptr/str aliases were removed
//             (2026-09-29, zero unqualified uses); write std::vector etc. directly.
// Test      : tests/utils/test_types_Types.cpp

#include <cstddef>
#include <cstdint>

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

}  // namespace swish
