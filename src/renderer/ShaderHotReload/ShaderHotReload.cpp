// ShaderHotReload: see the header for the design note. Compiled only in debug builds (SWISH_DEBUG_UI): the file is an
// empty translation unit in release, so the release binary is unchanged.
#ifdef SWISH_DEBUG_UI

#include "renderer/ShaderHotReload/ShaderHotReload.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <system_error>
#include <utility>

namespace swish {

namespace fs = std::filesystem;

// ── pure helpers ─────────────────────────────────────────────────────────────

std::string shader_stem(const fs::path& file) {
    return file.stem().string();
}

bool is_shader_source(const fs::path& file) {
    const std::string ext = file.extension().string();
    return ext == ".vert" || ext == ".frag" || ext == ".comp";
}

bool is_shared_include(const fs::path& shaderDir, const fs::path& file) {
    if (file.extension() != ".glsl")
        return false;
    // Lexical: <shaderDir>/common/<...>.glsl
    const fs::path rel = file.lexically_normal().lexically_relative((shaderDir / "common").lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}

fs::path spv_path(const fs::path& outputDir, const fs::path& source) {
    return outputDir / (source.filename().string() + ".spv");
}

std::vector<std::string> glslc_command(const GlslcSettings& s, const fs::path& source, const fs::path& outSpv) {
    std::vector<std::string> argv;
    argv.push_back(s.glslc);
    for (const std::string& d : s.defines)
        argv.push_back(d);
    argv.push_back("-I");
    argv.push_back((s.shaderDir / "common").string());
    argv.push_back("-I");
    argv.push_back(s.shaderDir.string());
    argv.push_back(source.string());
    argv.push_back("-o");
    argv.push_back(outSpv.string());
    return argv;
}

std::string shell_quote(const std::string& arg) {
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

std::string join_command(const std::vector<std::string>& argv) {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i)
            out += ' ';
        out += shell_quote(argv[i]);
    }
    return out;
}

std::string locate_glslc(const char* vulkanSdkEnv, const std::string& fallback,
                         const std::function<bool(const fs::path&)>& exists) {
    if (vulkanSdkEnv && *vulkanSdkEnv) {
        const fs::path sdk = fs::path(vulkanSdkEnv) / "bin" / "glslc";
        if (exists(sdk))
            return sdk.string();
    }
    if (!fallback.empty())
        return fallback;
    return "glslc";
}

std::vector<fs::path> MtimeTracker::update(const std::vector<FileStamp>& now) {
    std::vector<fs::path> changed;
    if (m_baseline) {
        for (const FileStamp& n : now) {
            auto it = std::find_if(m_last.begin(), m_last.end(), [&](const FileStamp& o) { return o.path == n.path; });
            if (it == m_last.end() || it->mtime != n.mtime)
                changed.push_back(n.path);
        }
    }
    m_last     = now;
    m_baseline = true;
    return changed;
}

// ── real edges ───────────────────────────────────────────────────────────────

ShaderHotReloadDeps default_hot_reload_deps() {
    ShaderHotReloadDeps d;
    d.now = [] {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    };
    d.scan = [](const fs::path& dir) {
        std::vector<FileStamp> out;
        std::error_code        ec;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            if (!is_shader_source(p) && p.extension() != ".glsl")
                continue;
            std::error_code tec;
            const auto      t = fs::last_write_time(p, tec);
            if (tec)
                continue;
            out.push_back({p, static_cast<std::int64_t>(t.time_since_epoch().count())});
        }
        return out;
    };
    d.compile = [](const std::vector<std::string>& argv) {
        CompileResult     r;
        const std::string cmd  = join_command(argv) + " 2>&1";
        FILE*             pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            r.exitCode = -1;
            r.output   = "could not start glslc";
            return r;
        }
        char buf[512];
        while (std::fgets(buf, sizeof buf, pipe))
            r.output += buf;
        const int status = pclose(pipe);
        r.exitCode       = status;  // 0 on success; any non-zero wait status is a failure
        return r;
    };
    return d;
}

ShaderHotReloadConfig default_hot_reload_config() {
    ShaderHotReloadConfig c;
#ifdef SWISH_SHADER_SRC_DIR
    c.shaderDir = SWISH_SHADER_SRC_DIR;
#endif
#ifdef SHADER_DIR
    c.outputDir = SHADER_DIR;
#endif
    std::string fallback;
#ifdef SWISH_GLSLC_PATH
    fallback = SWISH_GLSLC_PATH;
#endif
    c.glslc.glslc   = locate_glslc(std::getenv("VULKAN_SDK"), fallback, [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec);
    });
    c.glslc.defines = {"-DSWISH_DEBUG_UI"};  // this file only exists in debug builds, like CMake's SHADER_DEFINES
    return c;
}

// ── ShaderHotReload ──────────────────────────────────────────────────────────

ShaderHotReload::ShaderHotReload(ShaderHotReloadConfig cfg, ShaderHotReloadDeps deps)
    : m_cfg(std::move(cfg)), m_deps(std::move(deps)) {
    if (m_cfg.glslc.shaderDir.empty())
        m_cfg.glslc.shaderDir = m_cfg.shaderDir;
    if (m_cfg.shaderDir.empty() || m_cfg.outputDir.empty())
        m_enabled = false;  // built without the shader dirs (e.g. a unit-test target): nothing to watch
}

void ShaderHotReload::note(bool ok, const std::string& line) {
    m_status.lastOk      = ok;
    m_status.lastMessage = line;
    std::fprintf(stderr, "[ShaderHotReload] %s\n", line.c_str());
    m_log.push_back(line);
    constexpr std::size_t kMaxLog = 32;
    if (m_log.size() > kMaxLog)
        m_log.erase(m_log.begin(), m_log.begin() + static_cast<std::ptrdiff_t>(m_log.size() - kMaxLog));
}

std::size_t ShaderHotReload::poll() {
    if (!m_enabled)
        return 0;
    const double t = m_deps.now();
    if (t < m_nextPoll)
        return 0;
    m_nextPoll = t + m_cfg.pollIntervalSec;
    return poll_now();
}

std::size_t ShaderHotReload::poll_now() {
    if (!m_enabled)
        return 0;
    const std::vector<FileStamp> scan    = m_deps.scan(m_cfg.shaderDir);
    const std::vector<fs::path>  changed = m_tracker.update(scan);
    if (changed.empty())
        return 0;

    // What to recompile: the changed shader sources; a changed shared include means every shader.
    bool                  includeChanged = false;
    std::vector<fs::path> toBuild;
    for (const fs::path& p : changed) {
        if (is_shared_include(m_cfg.shaderDir, p))
            includeChanged = true;
        else if (is_shader_source(p))
            toBuild.push_back(p);
    }
    if (includeChanged) {
        // TODO(stub): use the glslc depfile to recompile only the shaders that include the touched file.
        toBuild.clear();
        for (const FileStamp& f : scan)
            if (is_shader_source(f.path))
                toBuild.push_back(f.path);
    }

    std::set<std::string> okStems;
    std::size_t           failures = 0;
    std::size_t           compiled = 0;
    for (const fs::path& src : toBuild) {
        const fs::path      out  = spv_path(m_cfg.outputDir, src);
        const CompileResult res  = m_deps.compile(glslc_command(m_cfg.glslc, src, out));
        const std::string   name = src.filename().string();
        m_status.lastShader      = name;
        if (res.exitCode == 0) {
            ++m_status.reloads;
            ++compiled;
            okStems.insert(shader_stem(src));
            note(true, "recompiled " + name);
        } else {
            ++m_status.compileFailures;
            ++failures;
            note(false, "glslc failed for " + name + ": " + res.output);  // previous .spv stays in use
        }
    }

    // Pipelines: all hooks for an include change (when everything compiled), else the hooks of the shaders that did.
    std::vector<std::string> errors;
    if (includeChanged && failures == 0) {
        if (m_rebuild.size() == 0)
            ++m_status.unhooked;
        m_status.rebuilds += static_cast<std::uint32_t>(m_rebuild.trigger_all(&errors));
        if (m_rebuild.size() > 0)
            note(errors.empty(), "shared include changed: ran all pipeline rebuild hooks");
    } else {
        for (const std::string& stem : okStems) {
            const std::size_t ran = m_rebuild.trigger(stem, &errors);
            m_status.rebuilds += static_cast<std::uint32_t>(ran);
            if (ran == 0) {
                ++m_status.unhooked;
                note(true, "TODO(stub): no pipeline rebuild hook for '" + stem + "' (restart to use the new .spv)");
            }
        }
    }
    for (const std::string& e : errors) {
        ++m_status.rebuildFailures;
        note(false, "pipeline rebuild failed: " + e);
    }
    return compiled;
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
