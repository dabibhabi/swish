#pragma once

#ifdef SWISH_DEBUG_UI

#include "DebugParams.h"

#include <string>
#include <vector>

namespace swish::debugio {

// Persist / restore a tuned look as a named TOML preset in a per-user directory
// (outside build/; see presets_dir()). Multiple named presets are supported (one file
// each). Scene fields only — transient UI state (editMode / showPanel /
// ssaaApplyRequested) is never written, and load() leaves any field absent from the
// file at its current value.

// Absolute directory presets are WRITTEN to (created on demand). Trailing slash.
// Resolution: $SWISH_PRESETS_DIR, else $XDG_CONFIG_HOME/swish/presets/, else
// $HOME/.config/swish/presets/, else (no home dir) the legacy CONFIG_DIR/presets/.
// load() / list_presets() also fall back to the legacy build-tree CONFIG_DIR/presets/
// so presets saved before the move remain usable; save() never writes there.
std::string presets_dir();

// Write `p` to <presets_dir>/<name>.toml. Returns false on I/O error.
bool save(const DebugParams& p, const std::string& name);

// Merge <presets_dir>/<name>.toml (else the legacy build-tree copy) into `p` (missing keys
// keep their value). Returns false if the file is missing or fails to parse.
bool load(DebugParams& p, const std::string& name);

// Preset names (filename without .toml) on disk in presets_dir() and the legacy dir,
// de-duplicated and sorted.
std::vector<std::string> list_presets();

}  // namespace swish::debugio

#endif  // SWISH_DEBUG_UI
