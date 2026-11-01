#pragma once

// Descriptor table for the single-source tunable registry (src/debug/Tunables.def).
// Header-only, no toml++/ImGui/Vulkan: safe to include from tests and release code.
// Member pointers (not offsetof) address the DebugParams field, so the table is a
// plain constexpr array consumed by generic save / load / UI / dump loops.

#include "DebugParams.h"
#include "utils/meta/Tags.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace swish::tunables {

enum class Kind : unsigned char { Bool, Float };

struct Tunable {
    Kind kind;
    bool DebugParams::* b;           // valid when kind == Bool
    float DebugParams::* f;          // valid when kind == Float
    const char*          name;       // C++ field name (dump line)
    const char*          group;      // TOML table
    const char*          key;        // TOML key
    const char*          label;      // ImGui label
    const char*          sliderFmt;  // ImGui slider format (floats)
    int                  dumpPrec;   // digits after '.' in the printf dump (floats)
    float                def;        // debug default (bools: 0/1)
    float                lo, hi;     // slider range (bools: 0/1)
    std::optional<float> release;    // `.release =` override; nullopt = same as def
    const char*          push;       // push-block member name or nullptr

    constexpr float release_default() const { return release.value_or(def); }
    constexpr bool  in_range(float v) const { return v >= lo && v <= hi; }
    constexpr bool  get_bool(const DebugParams& p) const { return p.*b; }
    constexpr float get_float(const DebugParams& p) const { return p.*f; }
};

namespace detail {
constexpr std::optional<float> rel_f(std::optional<float> r) { return r; }
constexpr std::optional<float> rel_f(std::optional<bool> r) {
    return r ? std::optional<float>(*r ? 1.0f : 0.0f) : std::nullopt;
}
}  // namespace detail

// clang-format off
#define PUSH(m) #m
#define NOPUSH nullptr
#define TUNE_B(n, g, k, def, label, rel) \
    Tunable{Kind::Bool, &DebugParams::n, nullptr, #n, g, k, label, nullptr, 0, (def) ? 1.0f : 0.0f, 0.0f, 1.0f, \
            detail::rel_f(std::optional<bool>(rel)), nullptr},
#define TUNE_F(n, g, k, def, lo, hi, label, sfmt, prec, push, rel) \
    Tunable{Kind::Float, nullptr, &DebugParams::n, #n, g, k, label, sfmt, prec, def, lo, hi, \
            detail::rel_f(std::optional<float>(rel)), push},
inline constexpr Tunable kTunables[] = {
#include "Tunables.def"
};
// clang-format on

inline constexpr std::span<const Tunable> all() { return std::span<const Tunable>(kTunables); }

// Tag dispatch: the ONE runtime branch on a tunable's value kind. Calls f(tag, value) with
// tag::flag + bool& (Kind::Bool) or tag::real + float& (Kind::Float); `p` may be const (the
// value is then const&). Save / load / ImGui widget / printf dump are each a small overload set
// on the tag (DebugParamsIO.cpp, DebugUI.cpp), so their loops carry no per-kind branch.
// No `default:` on purpose: a new Kind enumerator makes -Wswitch (an error under
// SWISH_WERROR) point at this function until the new tag is dispatched here.
template <class P, class F>
void visit(const Tunable& t, P& p, F&& f) {
    switch (t.kind) {
        case Kind::Bool:
            f(tag::flag, p.*t.b);
            return;
        case Kind::Float:
            f(tag::real, p.*t.f);
            return;
    }
}

// Compile-time sanity: every default (and release override) lies in its range.
constexpr bool defaults_in_range() {
    for (const Tunable& t : kTunables) {
        if (!t.in_range(t.def) || !t.in_range(t.release_default())) return false;
    }
    return true;
}
static_assert(defaults_in_range(), "Tunables.def: a default lies outside [lo, hi]");

// Debug-only, opt-in range check (NOT applied by load(): out-of-range values are still
// loaded verbatim, as pinned by tests). Returns the descriptors whose value in `p` is
// outside the slider range, for tools/tests that want a warning list.
inline std::size_t count_out_of_range(const DebugParams& p) {
    std::size_t n = 0;
    for (const Tunable& t : kTunables)
        if (t.kind == Kind::Float && !t.in_range(t.get_float(p))) ++n;
    return n;
}

}  // namespace swish::tunables
