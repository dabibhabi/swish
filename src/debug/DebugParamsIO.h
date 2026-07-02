#pragma once

#ifdef SWISH_DEBUG_UI

#include "DebugParams.h"

#include <string>
#include <vector>

namespace swish::debugio {

// Persist / restore a tuned look as a named TOML preset under CONFIG_DIR/presets/.
// Multiple named presets are supported (one file each). Scene fields only —
// transient UI state (editMode / showPanel / ssaaApplyRequested) is never written,
// and load() leaves any field absent from the file at its current value.

// Absolute directory presets live in (created on demand). Trailing slash.
std::string presets_dir();

// Write `p` to <presets_dir>/<name>.toml. Returns false on I/O error.
bool save(const DebugParams& p, const std::string& name);

// Merge <presets_dir>/<name>.toml into `p` (missing keys keep their value).
// Returns false if the file is missing or fails to parse.
bool load(DebugParams& p, const std::string& name);

// Preset names (filename without .toml) currently on disk, sorted.
std::vector<std::string> list_presets();

}  // namespace swish::debugio

#endif  // SWISH_DEBUG_UI
