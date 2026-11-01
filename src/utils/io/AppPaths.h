#pragma once

// ── AppPaths.h ───────────────────────────────────────────────────────
// Purpose   : where the runtime finds shaders/, assets/, textures/ and config/, so one binary runs from the
//             build tree, a folder next to it, or a macOS .app bundle (Contents/Resources/).
// Resolution: for each directory name, the first that exists of  <exe>/../Resources/<name>/  (bundle),
//             <exe>/<name>/ (folder), then the compile-time build-tree macro (SHADER_DIR, ASSET_DIR, ...).
//             The build tree therefore resolves exactly as before.
// Units     : none (paths). Every accessor returns a path with a trailing '/'.
// Complexity: a few stat() calls per call; callers hold the result, not a cache.
// Errors    : missing directories never throw; they fall through to the next candidate.
// See also  : scripts/package_mac.sh (builds the bundle), docs/packaging.md
// Test      : tests/utils/test_io_AppPaths.cpp (resolve_dir only; the macro fallbacks are build-specific)

#include <filesystem>
#include <string>
#include <string_view>

namespace swish::util::paths {

// Directory of the running executable (symlinks resolved). Empty if it cannot be determined.
[[nodiscard]] std::filesystem::path executable_dir();

// Pure resolution step, exposed for tests: <exeDir>/../Resources/<name>/, then <exeDir>/<name>/, else `fallback`.
// An empty exeDir skips the lookup, so a relative cwd can never match.
[[nodiscard]] std::string resolve_dir(const std::filesystem::path& exeDir, std::string_view name,
                                      std::string_view fallback);

[[nodiscard]] std::string shader_dir();   // SPIR-V (*.spv)
[[nodiscard]] std::string asset_dir();    // GLB / HDR / corridor bake
[[nodiscard]] std::string texture_dir();  // PBR texture set
[[nodiscard]] std::string config_dir();   // car.toml, road.bin

// Per-user writable cache (created on demand): ~/Library/Caches/Swish on macOS, $XDG_CACHE_HOME/swish or
// ~/.cache/swish elsewhere. Falls back to config_dir() when no home directory is known. Never inside a bundle.
[[nodiscard]] std::string user_cache_dir();

// Call once at startup, before any Vulkan call. In a bundle, points the Vulkan loader at the bundled MoltenVK
// ICD manifest (VK_ICD_FILENAMES / VK_DRIVER_FILES) unless the user already set one. No-op elsewhere.
void configure_process_environment();

}  // namespace swish::util::paths
