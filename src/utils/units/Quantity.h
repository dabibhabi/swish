#pragma once

// ── Quantity.h ───────────────────────────────────────────────────────
// Purpose   : zero-cost strong type over a float so metres, seconds, degrees, rpm, WU ... cannot be mixed up:
//             `Metres + Seconds` does not compile, `Metres m = 3.0f;` does not compile. For module APIs only.
// Units     : set by the tag (see `dim` and the aliases below). The ONLY place a value changes tag is Units.h
//             (to_wu, to_metres, to_radians, to_degrees).
// Complexity: O(1), constexpr, sizeof(Quantity<T>) == sizeof(float), trivially copyable.
// Precision : each operator is exactly ONE float operation. Never use Quantity inside mesh generators or hot loops:
//             `a * b + c` split across two inline operators is no longer one source expression, so Clang stops fusing
//             it into an FMA and the last bit changes (goldens are byte-checked). Unwrap with .value() at the top.
// Example   : namespace uu = swish::util::units;
//             uu::Metres d{3.0f};  d += uu::Metres{1.5f};  float r = d / uu::Metres{1.5f};  // r == 3.0f
// See also  : Units.h, docs/tutorials/typed-cpp/03-strong-types.md
// Test      : tests/utils/test_units_Quantity.cpp

#include <compare>

namespace swish::util::units {


namespace dim {
    struct wu;
    struct metre;
    struct metre_per_second;
    struct second;
    struct radian;
    struct degree;
    struct rpm;
}  

template <class Tag>
class Quantity {
public:
    using tag = Tag;

    constexpr Quantity() noexcept = default;
    constexpr explicit Quantity(float v) noexcept : m_v(v) {}

    [[nodiscard]] constexpr float value() const noexcept { return m_v; }

    
    constexpr auto& operator+=(this auto& self, Quantity o) noexcept {
        self.m_v += o.m_v;
        return self;
    }
    constexpr auto& operator-=(this auto& self, Quantity o) noexcept {
        self.m_v -= o.m_v;
        return self;
    }
    constexpr auto& operator*=(this auto& self, float k) noexcept {
        self.m_v *= k;
        return self;
    }
    constexpr auto& operator/=(this auto& self, float k) noexcept {
        self.m_v /= k;
        return self;
    }

    friend constexpr Quantity operator+(Quantity a, Quantity b) noexcept { return a += b; }
    friend constexpr Quantity operator-(Quantity a, Quantity b) noexcept { return a -= b; }
    friend constexpr Quantity operator-(Quantity a) noexcept { return Quantity{-a.m_v}; }
    friend constexpr Quantity operator*(Quantity a, float k) noexcept { return a *= k; }
    friend constexpr Quantity operator*(float k, Quantity a) noexcept { return a *= k; }
    friend constexpr Quantity operator/(Quantity a, float k) noexcept { return a /= k; }
    
    friend constexpr float operator/(Quantity a, Quantity b) noexcept { return a.m_v / b.m_v; }

    
    constexpr auto operator<=>(const Quantity&) const noexcept = default;

private:
    float m_v = 0.0f;
};

using Wu              = Quantity<dim::wu>;
using Metres          = Quantity<dim::metre>;
using MetresPerSecond = Quantity<dim::metre_per_second>;
using Seconds         = Quantity<dim::second>;
using Radians         = Quantity<dim::radian>;
using Degrees         = Quantity<dim::degree>;
using Rpm             = Quantity<dim::rpm>;

} 
