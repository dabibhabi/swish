#pragma once

// ── TomlGlm.h ────────────────────────────────────────────────────────
// Purpose   : glue between glm::vec3 and toml++ arrays, used by the debug-parameter save/load.
//             Everything is inside #ifdef SWISH_DEBUG_UI, so release builds never see toml++.
// Units     : none; components are stored as TOML doubles (float precision is fine for presets).
// Complexity: O(1), one small allocation for the TOML array on write.
// Example   : namespace ud = swish::util::debug;
//             tbl.insert("color", ud::vec3_to_toml(p.sunColor));
//             ud::read_vec3(tbl["sun"]["color"], p.sunColor);   // keeps p.sunColor if absent/malformed
// See also  : debug/DebugParamsIO.cpp (only user)
// Test      : tests/utils/test_debug_TomlGlm.cpp (built into swish_debug_tests; debug-UI config only)

#ifdef SWISH_DEBUG_UI

#include <glm/glm.hpp>
#include <toml++/toml.hpp>

namespace swish::util::debug {

// vec3 -> 3-element TOML array (stored as doubles).
inline toml::array vec3_to_toml(const glm::vec3& v) {
    return toml::array{v.x, v.y, v.z};
}

// Read a 3-element array node into `v`, keeping `v` if the node is absent or malformed.
inline void read_vec3(toml::node_view<toml::node> n, glm::vec3& v) {
    const toml::array* a = n.as_array();
    if (!a || a->size() != 3)
        return;
    v.x = static_cast<float>(a->get(0)->value_or(static_cast<double>(v.x)));
    v.y = static_cast<float>(a->get(1)->value_or(static_cast<double>(v.y)));
    v.z = static_cast<float>(a->get(2)->value_or(static_cast<double>(v.z)));
}

}  // namespace swish::util::debug

#endif  // SWISH_DEBUG_UI
