// renderer/ShaderHotReload + PipelineRebuild: GPU-free. The clock, directory scan and glslc runner are injected, so
// mtime detection, rate limiting, stem mapping, command-line construction, hook dispatch and failure handling are
// all checked without touching the file system or Vulkan.
#include "renderer/PipelineRebuild/PipelineRebuild.h"
#include "renderer/ShaderHotReload/ShaderHotReload.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using namespace swish;
namespace fs = std::filesystem;

namespace {

const fs::path kShaders = "/repo/shaders";
const fs::path kOut     = "/build/shaders";

struct Fake {
    double                                time = 0.0;
    std::vector<FileStamp>                files;
    std::vector<std::vector<std::string>> compiles;  // argv of each compile
    std::vector<std::string>              failFor;   // source file names that fail
    ShaderHotReloadDeps                   deps;

    Fake() {
        deps.now     = [this] { return time; };
        deps.scan    = [this](const fs::path&) { return files; };
        deps.compile = [this](const std::vector<std::string>& argv) {
            compiles.push_back(argv);
            CompileResult r;
            for (const std::string& f : failFor)
                if (fs::path(argv[argv.size() - 3]).filename() == f) {
                    r.exitCode = 1;
                    r.output   = "error: syntax";
                }
            return r;
        };
    }
    void touch(const char* rel, std::int64_t mtime) {
        const fs::path p = kShaders / rel;
        for (FileStamp& f : files)
            if (f.path == p) {
                f.mtime = mtime;
                return;
            }
        files.push_back({p, mtime});
    }
};

ShaderHotReloadConfig make_cfg() {
    ShaderHotReloadConfig c;
    c.shaderDir       = kShaders;
    c.outputDir       = kOut;
    c.glslc.glslc     = "/sdk/bin/glslc";
    c.glslc.defines   = {"-DSWISH_DEBUG_UI"};
    c.pollIntervalSec = 0.5;
    return c;
}

}  // namespace

TEST_CASE("shader stem mapping and file classification", "[renderer][hotreload]") {
    CHECK(shader_stem("/repo/shaders/composite.frag") == "composite");
    CHECK(shader_stem("fullscreen.vert") == "fullscreen");
    CHECK(is_shader_source("a.vert"));
    CHECK(is_shader_source("a.frag"));
    CHECK(is_shader_source("a.comp"));
    CHECK_FALSE(is_shader_source("a.glsl"));
    CHECK_FALSE(is_shader_source("README.md"));
    CHECK(is_shared_include(kShaders, kShaders / "common" / "bindings.glsl"));
    CHECK(is_shared_include(kShaders, kShaders / "common" / "sub" / "x.glsl"));
    CHECK_FALSE(is_shared_include(kShaders, kShaders / "other.glsl"));
    CHECK_FALSE(is_shared_include(kShaders, kShaders / "common" / "a.frag"));
    CHECK_FALSE(is_shared_include(kShaders, "/elsewhere/common/bindings.glsl"));
    CHECK(spv_path(kOut, kShaders / "composite.frag") == fs::path("/build/shaders/composite.frag.spv"));
}

TEST_CASE("glslc command line matches the CMake rule", "[renderer][hotreload]") {
    GlslcSettings s;
    s.glslc                             = "/sdk/bin/glslc";
    s.defines                           = {"-DSWISH_DEBUG_UI"};
    s.shaderDir                         = kShaders;
    const auto                     argv = glslc_command(s, kShaders / "composite.frag", kOut / "composite.frag.spv");
    const std::vector<std::string> expected = {"/sdk/bin/glslc",
                                               "-DSWISH_DEBUG_UI",
                                               "-I",
                                               "/repo/shaders/common",
                                               "-I",
                                               "/repo/shaders",
                                               "/repo/shaders/composite.frag",
                                               "-o",
                                               "/build/shaders/composite.frag.spv"};
    CHECK(argv == expected);

    CHECK(shell_quote("a b") == "'a b'");
    CHECK(shell_quote("it's") == "'it'\\''s'");
    CHECK(join_command({"glslc", "x y"}) == "'glslc' 'x y'");
}

TEST_CASE("glslc lookup: SDK first, then the CMake path, then PATH", "[renderer][hotreload]") {
    auto yes = [](const fs::path&) { return true; };
    auto no  = [](const fs::path&) { return false; };
    CHECK(locate_glslc("/sdk", "/brew/glslc", yes) == "/sdk/bin/glslc");
    CHECK(locate_glslc("/sdk", "/brew/glslc", no) == "/brew/glslc");
    CHECK(locate_glslc(nullptr, "/brew/glslc", yes) == "/brew/glslc");
    CHECK(locate_glslc("", "", yes) == "glslc");
}

TEST_CASE("MtimeTracker: baseline first, then only added or modified files", "[renderer][hotreload]") {
    MtimeTracker t;
    CHECK_FALSE(t.has_baseline());
    std::vector<FileStamp> files = {{"a.frag", 1}, {"b.frag", 1}};
    CHECK(t.update(files).empty());  // baseline
    CHECK(t.has_baseline());
    CHECK(t.update(files).empty());  // unchanged

    files[1].mtime = 2;
    auto c         = t.update(files);
    REQUIRE(c.size() == 1);
    CHECK(c[0] == fs::path("b.frag"));
    CHECK(t.update(files).empty());  // reported once

    files.push_back({"c.comp", 5});
    c = t.update(files);
    REQUIRE(c.size() == 1);
    CHECK(c[0] == fs::path("c.comp"));
}

TEST_CASE("PipelineRebuild dispatches by stem, isolates failures", "[renderer][hotreload]") {
    PipelineRebuild r;
    int             a = 0, b = 0;
    r.register_rebuild("composite", [&] { ++a; });
    r.register_rebuild("composite", [&] { ++a; });
    r.register_rebuild("ssr", [&] {
        ++b;
        throw std::runtime_error("boom");
    });
    CHECK(r.size() == 3);
    CHECK(r.has("composite"));
    CHECK_FALSE(r.has("dof"));

    CHECK(r.trigger("composite") == 2);
    CHECK(a == 2);
    CHECK(r.trigger("dof") == 0);

    std::vector<std::string> errors;
    CHECK(r.trigger("ssr", &errors) == 1);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] == "ssr: boom");

    errors.clear();
    CHECK(r.trigger_all(&errors) == 3);
    CHECK(a == 4);
    CHECK(errors.size() == 1);
}

TEST_CASE("hot reload is rate limited and does not recompile at start-up", "[renderer][hotreload]") {
    Fake f;
    f.touch("composite.frag", 1);
    ShaderHotReload hr(make_cfg(), f.deps);

    CHECK(hr.poll() == 0);  // baseline scan
    f.touch("composite.frag", 2);
    f.time = 0.2;  // inside the 0.5 s window: not even scanned
    CHECK(hr.poll() == 0);
    CHECK(f.compiles.empty());
    f.time = 0.6;
    CHECK(hr.poll() == 1);
    CHECK(f.compiles.size() == 1);
}

TEST_CASE("a changed shader is recompiled and its hook runs; unhooked shaders log TODO(stub)",
          "[renderer][hotreload]") {
    Fake f;
    f.touch("composite.frag", 1);
    f.touch("ssr.frag", 1);
    ShaderHotReload hr(make_cfg(), f.deps);
    int             composite = 0;
    hr.register_rebuild("composite", [&] { ++composite; });
    hr.poll_now();  // baseline

    f.touch("composite.frag", 2);
    CHECK(hr.poll_now() == 1);
    CHECK(composite == 1);
    REQUIRE(f.compiles.size() == 1);
    CHECK(f.compiles[0].back() == "/build/shaders/composite.frag.spv");
    CHECK(hr.status().reloads == 1);
    CHECK(hr.status().rebuilds == 1);
    CHECK(hr.status().lastOk);
    CHECK(hr.status().lastShader == "composite.frag");

    f.touch("ssr.frag", 2);
    CHECK(hr.poll_now() == 1);
    CHECK(composite == 1);  // untouched
    CHECK(hr.status().unhooked == 1);
    CHECK(hr.status().lastMessage.find("TODO(stub)") != std::string::npos);
}

TEST_CASE("a compile error is reported, runs no hook and does not stop later reloads", "[renderer][hotreload]") {
    Fake f;
    f.touch("composite.frag", 1);
    ShaderHotReload hr(make_cfg(), f.deps);
    int             composite = 0;
    hr.register_rebuild("composite", [&] { ++composite; });
    hr.poll_now();

    f.failFor = {"composite.frag"};
    f.touch("composite.frag", 2);
    CHECK(hr.poll_now() == 0);
    CHECK(composite == 0);
    CHECK_FALSE(hr.status().lastOk);
    CHECK(hr.status().compileFailures == 1);
    CHECK(hr.status().lastMessage.find("syntax") != std::string::npos);

    f.failFor.clear();  // fixed on disk: the next save retries
    f.touch("composite.frag", 3);
    CHECK(hr.poll_now() == 1);
    CHECK(composite == 1);
    CHECK(hr.status().lastOk);
}

TEST_CASE("a throwing rebuild hook is caught and counted", "[renderer][hotreload]") {
    Fake f;
    f.touch("composite.frag", 1);
    ShaderHotReload hr(make_cfg(), f.deps);
    hr.register_rebuild("composite", [] { throw std::runtime_error("bad spv"); });
    hr.poll_now();
    f.touch("composite.frag", 2);
    CHECK_NOTHROW(hr.poll_now());
    CHECK(hr.status().rebuildFailures == 1);
    CHECK_FALSE(hr.status().lastOk);
}

TEST_CASE("a shared include recompiles every shader and triggers all hooks", "[renderer][hotreload]") {
    Fake f;
    f.touch("composite.frag", 1);
    f.touch("ssr.frag", 1);
    f.touch("fullscreen.vert", 1);
    f.touch("common/bindings.glsl", 1);
    ShaderHotReload hr(make_cfg(), f.deps);
    int             composite = 0, ssr = 0;
    hr.register_rebuild("composite", [&] { ++composite; });
    hr.register_rebuild("ssr", [&] { ++ssr; });
    hr.poll_now();

    f.touch("common/bindings.glsl", 2);
    CHECK(hr.poll_now() == 3);  // all three shader sources, not the include itself
    CHECK(f.compiles.size() == 3);
    CHECK(composite == 1);
    CHECK(ssr == 1);
}

TEST_CASE("disabled hot reload (or missing dirs) does nothing", "[renderer][hotreload]") {
    Fake f;
    f.touch("composite.frag", 1);
    ShaderHotReload hr(make_cfg(), f.deps);
    hr.poll_now();
    hr.set_enabled(false);
    f.touch("composite.frag", 2);
    CHECK(hr.poll_now() == 0);
    CHECK(f.compiles.empty());

    ShaderHotReloadConfig empty;  // no dirs (a target built without the shader paths)
    ShaderHotReload       none(empty, f.deps);
    CHECK_FALSE(none.enabled());
}
