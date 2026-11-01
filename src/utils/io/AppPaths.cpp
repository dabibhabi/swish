#include "AppPaths.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace swish::util::paths {

namespace {

// Macro-free fallbacks: a target that does not define a *_DIR macro gets "" (and so, for that directory, the
// resolution simply finds nothing and returns "").
#ifdef SHADER_DIR
constexpr std::string_view kShaderFallback = SHADER_DIR;
#else
constexpr std::string_view kShaderFallback;
#endif
#ifdef ASSET_DIR
constexpr std::string_view kAssetFallback = ASSET_DIR;
#else
constexpr std::string_view kAssetFallback;
#endif
#ifdef TEXTURE_DIR
constexpr std::string_view kTextureFallback = TEXTURE_DIR;
#else
constexpr std::string_view kTextureFallback;
#endif
#ifdef CONFIG_DIR
constexpr std::string_view kConfigFallback = CONFIG_DIR;
#else
constexpr std::string_view kConfigFallback;
#endif

std::filesystem::path bundle_resources(const std::filesystem::path& exeDir) {
    return exeDir / ".." / "Resources";
}

}  // namespace

std::filesystem::path executable_dir() {
    std::filesystem::path exe;
#if defined(__APPLE__)
    uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    exe = buf.c_str();
#else
    std::error_code ec;
    exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
#endif
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(exe, ec);
    if (ec) return {};
    return canonical.parent_path();
}

std::string resolve_dir(const std::filesystem::path& exeDir, std::string_view name, std::string_view fallback) {
    if (exeDir.empty()) return std::string(fallback);
    for (const auto& base : {bundle_resources(exeDir), exeDir}) {
        std::error_code ec;
        const auto dir = base / name;
        if (std::filesystem::is_directory(dir, ec)) return dir.lexically_normal().string() + "/";
    }
    return std::string(fallback);
}

std::string shader_dir() { return resolve_dir(executable_dir(), "shaders", kShaderFallback); }
std::string asset_dir() { return resolve_dir(executable_dir(), "assets", kAssetFallback); }
std::string texture_dir() { return resolve_dir(executable_dir(), "textures", kTextureFallback); }
std::string config_dir() { return resolve_dir(executable_dir(), "config", kConfigFallback); }

std::string user_cache_dir() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return config_dir();

    std::filesystem::path dir;
#if defined(__APPLE__)
    dir = std::filesystem::path(home) / "Library" / "Caches" / "Swish";
#else
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    dir = (xdg && *xdg) ? std::filesystem::path(xdg) / "swish" : std::filesystem::path(home) / ".cache" / "swish";
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);  // best effort: a failed write just means no cache
    return dir.string() + "/";
}

void configure_process_environment() {
    const auto icd = bundle_resources(executable_dir()) / "vulkan" / "icd.d" / "MoltenVK_icd.json";
    std::error_code ec;
    if (executable_dir().empty() || !std::filesystem::is_regular_file(icd, ec)) return;
    const std::string manifest = icd.lexically_normal().string();
    // overwrite = 0: an explicit loader setting from the user wins over the bundled driver.
    setenv("VK_ICD_FILENAMES", manifest.c_str(), 0);
    setenv("VK_DRIVER_FILES", manifest.c_str(), 0);
}

}  // namespace swish::util::paths
