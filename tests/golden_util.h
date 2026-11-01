#pragma once
// Shared helpers for golden-file tests (tests/golden/).
//
// Goldens are plain "key value" text files (one pair per line, '#' comments).
// Run the test binary with SWISH_UPDATE_GOLDENS=1 to (re)write them instead of
// comparing. Goldens are bound to the compiler/stdlib/libm they were generated
// with (float rounding), so regenerate deliberately and review the diff.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace golden {

inline bool update_mode() {
    const char* e = std::getenv("SWISH_UPDATE_GOLDENS");
    return e && *e && *e != '0';
}

inline std::string path(const std::string& file) { return std::string(GOLDEN_DIR) + file; }

// FNV-1a 64-bit.
struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void     bytes(const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void u32(uint32_t v) { bytes(&v, sizeof v); }
    // Quantise to 0.01 units so sub-ulp libm differences do not flip the hash.
    void q(float f) {
        const int32_t v = static_cast<int32_t>(std::lround(static_cast<double>(f) * 100.0));
        bytes(&v, sizeof v);
    }
};

inline std::string hex(uint64_t v) {
    std::ostringstream o;
    o << std::hex << v;
    return o.str();
}

using KV = std::map<std::string, std::string>;

inline KV read(const std::string& file) {
    KV            kv;
    std::ifstream f(path(file));
    std::string   line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        kv[line.substr(0, sp)] = line.substr(sp + 1);
    }
    return kv;
}

inline void write(const std::string& file, const KV& kv, const std::string& header) {
    std::ofstream f(path(file), std::ios::trunc);
    f << "# " << header << "\n# Regenerate: SWISH_UPDATE_GOLDENS=1 build/tests/swish_tests \"[golden]\"\n";
    for (const auto& [k, v] : kv) f << k << ' ' << v << '\n';
}

}  // namespace golden
