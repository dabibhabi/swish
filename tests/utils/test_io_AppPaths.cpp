#include "utils/io/AppPaths.h"

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace paths = swish::util::paths;
namespace fs    = std::filesystem;

namespace {

// A throwaway app layout under the temp dir: <root>/Contents/MacOS is the "exe" dir.
struct Layout {
    fs::path root = fs::temp_directory_path() / ("swish_apppaths_" + std::to_string(std::hash<std::string>{}(__FILE__)) +
                                                  "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::path exeDir() const { return root / "Contents" / "MacOS"; }
    Layout() { fs::create_directories(exeDir()); }
    ~Layout() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

}  // namespace

TEST_CASE("resolve_dir prefers the .app bundle Resources, then the folder beside the exe", "[apppaths]") {
    Layout l;
    const std::string fallback = "/build/tree/shaders/";

    SECTION("nothing on disk falls back to the build-tree macro") {
        CHECK(paths::resolve_dir(l.exeDir(), "shaders", fallback) == fallback);
    }

    SECTION("a folder next to the exe is used") {
        fs::create_directories(l.exeDir() / "shaders");
        CHECK(paths::resolve_dir(l.exeDir(), "shaders", fallback) == (l.exeDir() / "shaders").string() + "/");
    }

    SECTION("bundle Resources beats the folder next to the exe") {
        fs::create_directories(l.exeDir() / "shaders");
        fs::create_directories(l.root / "Contents" / "Resources" / "shaders");
        CHECK(paths::resolve_dir(l.exeDir(), "shaders", fallback) ==
              (l.root / "Contents" / "Resources" / "shaders").lexically_normal().string() + "/");
    }
}

TEST_CASE("resolve_dir ignores a plain file of the same name", "[apppaths]") {
    Layout l;
    { std::ofstream f(l.exeDir() / "config"); }
    CHECK(paths::resolve_dir(l.exeDir(), "config", "/fallback/") == "/fallback/");
}

TEST_CASE("resolve_dir with an unknown exe dir never reads the cwd", "[apppaths]") {
    // An empty exe dir would make "../Resources" relative to the working directory; it must not.
    CHECK(paths::resolve_dir(fs::path{}, "shaders", "/fallback/") == "/fallback/");
}
