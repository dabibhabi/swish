#pragma once
// ShaderHotReload (debug builds only): polls the mtimes of the files under shaders/, recompiles a changed shader to
// SPIR-V with the same glslc invocation CMake uses, writes the .spv where the app loads it (SHADER_DIR, the build
// dir) and then runs the registered PipelineRebuild hook(s) for that shader stem.
//
// Design note (S5, stub):
//  - Everything except the file system / process / clock is pure and unit-tested GPU-free (tests/utils): the
//    mtime change detector (MtimeTracker), the stem mapping, the glslc command line, glslc lookup. The three impure
//    edges (clock, directory scan, process spawn) are injectable through ShaderHotReloadDeps.
//  - The class is only COMPILED under SWISH_DEBUG_UI (ShaderHotReload.cpp is an empty TU in release, so the release
//    binary is unchanged). The header has no #ifdef, so it is ODR-safe; the unit tests compile the .cpp with the
//    define set on that one source file.
//  - poll() is called once per frame; it is rate limited (default 0.5 s) and never throws: a compile error is logged
//    and kept in status() so the debug UI can show it, and the previous .spv stays in use.
//  - A change to shaders/common/*.glsl recompiles EVERY shader and triggers ALL registered hooks.
//
// TODO(stub): proper dependency tracking (use glslc -MD depfiles to recompile only the shaders that include the
//             touched file); compile on a worker thread (poll() blocks for the glslc run); watch for deleted/added
//             shaders; only Composite has a real rebuild hook today (Renderer registers it), every other shader logs
//             a TODO(stub) line and needs an app restart.

#include "renderer/PipelineRebuild/PipelineRebuild.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace swish {

// ── pure helpers ─────────────────────────────────────────────────────────────

struct FileStamp {
    std::filesystem::path path;
    std::int64_t          mtime = 0;  // opaque, only compared for equality (file_time_type ticks)
};

// "shaders/composite.frag" -> "composite". Used to key PipelineRebuild hooks.
std::string shader_stem(const std::filesystem::path& file);
// .vert / .frag / .comp (the files CMake globs and compiles).
bool is_shader_source(const std::filesystem::path& file);
// A *.glsl under <shaderDir>/common (pulled in with #include; changing one affects every shader).
bool is_shared_include(const std::filesystem::path& shaderDir, const std::filesystem::path& file);
// Output path of a shader: <outputDir>/<filename>.spv (same naming as the CMake rule: composite.frag.spv).
std::filesystem::path spv_path(const std::filesystem::path& outputDir, const std::filesystem::path& source);

struct GlslcSettings {
    std::string              glslc = "glslc";
    std::vector<std::string> defines;    // e.g. "-DSWISH_DEBUG_UI" (same as CMake's SHADER_DEFINES)
    std::filesystem::path    shaderDir;  // -I <shaderDir>/common -I <shaderDir>, same as CMake's SHADER_INCLUDE_ARGS
};

// argv of the compile: glslc <defines> -I common -I shaders <src> -o <out>  (CMake rule minus the depfile flags).
std::vector<std::string> glslc_command(const GlslcSettings& s, const std::filesystem::path& source,
                                       const std::filesystem::path& outSpv);
// POSIX-shell single-quoting of one argument, and the joined command line (used to spawn through popen).
std::string shell_quote(const std::string& arg);
std::string join_command(const std::vector<std::string>& argv);

// glslc lookup order: $VULKAN_SDK/bin/glslc (when it exists), then `fallback` (CMake's find_program result), then
// plain "glslc" on PATH. `exists` is injectable so the order is testable.
std::string locate_glslc(const char* vulkanSdkEnv, const std::string& fallback,
                         const std::function<bool(const std::filesystem::path&)>& exists);

// Detects added / modified files between two scans. The first update() only records the baseline (returns {}), so
// start-up never recompiles. Not a deletion watcher.
class MtimeTracker {
public:
    std::vector<std::filesystem::path> update(const std::vector<FileStamp>& now);
    bool                               has_baseline() const { return m_baseline; }

private:
    std::vector<FileStamp> m_last;
    bool                   m_baseline = false;
};

// ── hot reload ───────────────────────────────────────────────────────────────

struct CompileResult {
    int         exitCode = 0;
    std::string output;  // glslc stdout+stderr (the error text on failure)
};

struct ShaderHotReloadConfig {
    std::filesystem::path shaderDir;  // source dir to watch (the repo's shaders/)
    std::filesystem::path outputDir;  // where the app loads .spv from (SHADER_DIR)
    GlslcSettings         glslc;      // .shaderDir is filled from shaderDir by the constructor when empty
    double                pollIntervalSec = 0.5;
};

struct ShaderHotReloadDeps {
    std::function<double()>                                             now;      // seconds, monotonic
    std::function<std::vector<FileStamp>(const std::filesystem::path&)> scan;     // files under shaderDir
    std::function<CompileResult(const std::vector<std::string>&)>       compile;  // runs argv, captures output
};
// The real clock (steady_clock), directory scan (std::filesystem) and process runner (popen).
ShaderHotReloadDeps default_hot_reload_deps();
// Config from the build: SWISH_SHADER_SRC_DIR / SHADER_DIR / SWISH_GLSLC_PATH compile definitions + $VULKAN_SDK.
ShaderHotReloadConfig default_hot_reload_config();

class ShaderHotReload {
public:
    // Plain status for the debug UI (read-only accessors below).
    struct Status {
        std::uint32_t reloads         = 0;  // shaders recompiled successfully
        std::uint32_t compileFailures = 0;
        std::uint32_t rebuilds        = 0;  // pipeline rebuild hooks run (successfully or not)
        std::uint32_t rebuildFailures = 0;
        std::uint32_t unhooked        = 0;     // reloads with no hook (TODO(stub): need a restart)
        bool          lastOk          = true;  // result of the most recent event
        std::string   lastShader;              // file name of the most recent compile
        std::string   lastMessage;             // one line: "recompiled composite.frag", or the glslc error
    };

    explicit ShaderHotReload(ShaderHotReloadConfig cfg  = default_hot_reload_config(),
                             ShaderHotReloadDeps   deps = default_hot_reload_deps());

    // Hook registry (see PipelineRebuild.h). Key = shader stem ("composite").
    void register_rebuild(const std::string& stem, PipelineRebuild::Fn fn) {
        m_rebuild.register_rebuild(stem, std::move(fn));
    }
    const PipelineRebuild& rebuild_registry() const { return m_rebuild; }

    // Call once per frame, from the main thread, between frames (a hook recreates pipelines under
    // vkDeviceWaitIdle). Rate limited; returns the number of shaders recompiled this call.
    std::size_t poll();

    // Run one scan now, ignoring the rate limit (tests, an "check now" UI button).
    std::size_t poll_now();

    bool                         enabled() const { return m_enabled; }
    void                         set_enabled(bool e) { m_enabled = e; }
    const Status&                status() const { return m_status; }
    const ShaderHotReloadConfig& config() const { return m_cfg; }
    // Most recent log lines (oldest first, capped), the same text printed to stderr.
    const std::vector<std::string>& log() const { return m_log; }

private:
    void note(bool ok, const std::string& line);

    ShaderHotReloadConfig    m_cfg;
    ShaderHotReloadDeps      m_deps;
    PipelineRebuild          m_rebuild;
    MtimeTracker             m_tracker;
    Status                   m_status;
    std::vector<std::string> m_log;
    double                   m_nextPoll = 0.0;
    bool                     m_enabled  = true;
};

}  // namespace swish
