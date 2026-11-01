#pragma once

// Purpose:    Fixed-capacity telemetry ring buffer (no allocation after construction) plus CSV
//             export/import, so a drive can be recorded, saved, and replayed. GPU-free, std only.
// Units:      Sample fields are stored as the car exposes them: pos in WU (1 m = 1000 WU,
//             utils/units/Units.h), speed in m/s, angles in degrees, t in seconds.
// Complexity: push O(1); ordered iteration O(n); CSV O(n).
// Example:    TelemetryRing ring(3600); ring.push(sample); write_csv(ring, make_timestamped_csv_path());
// See also:   utils/telemetry/ReplayDriver.h, debug/TelemetryRecorder.h, utils/input/InputFrame.h.
// Test:       tests/test_telemetry.cpp
//
// TODO(stub): binary format, compression, per-wheel slip/load channels, decimation (record every Nth step).

#include "utils/input/InputFrame.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace swish {

struct TelemetrySample {
    float                t         = 0.f;  // sim time (s)
    float                x         = 0.f;  // position (WU)
    float                y         = 0.f;
    float                z         = 0.f;
    float                yaw_deg   = 0.f;
    float                speed_mps = 0.f;
    int                  gear      = 0;  // 0-indexed
    float                rpm       = 0.f;
    float                steer_deg = 0.f;
    float                throttle  = 0.f;
    float                brake     = 0.f;
    float                mu        = 0.f;  // effective tire-road friction
    std::array<float, 4> wheel_spin{};     // rad, [0,2pi), 0 FL 1 FR 2 BL 3 BR
    std::array<float, 4> susp{};           // per-corner suspension travel (m)
    InputFrame           input{};          // the driver command used for this step
};

class TelemetryRing {
public:
    explicit TelemetryRing(std::size_t capacity = 3600) : m_buf(capacity == 0 ? 1 : capacity) {}

    std::size_t capacity() const { return m_buf.size(); }
    std::size_t size() const { return m_size; }
    bool        empty() const { return m_size == 0; }
    void        clear() {
        m_head = 0;
        m_size = 0;
    }

    // Append; when full the oldest sample is overwritten.
    void push(const TelemetrySample& s) {
        m_buf[m_head] = s;
        m_head        = (m_head + 1) % m_buf.size();
        if (m_size < m_buf.size()) ++m_size;
    }

    // i = 0 is the OLDEST retained sample, size()-1 the newest. Precondition: i < size().
    const TelemetrySample& at(std::size_t i) const {
        const std::size_t start = (m_head + m_buf.size() - m_size) % m_buf.size();
        return m_buf[(start + i) % m_buf.size()];
    }
    const TelemetrySample& oldest() const { return at(0); }
    const TelemetrySample& newest() const { return at(m_size - 1); }

private:
    std::vector<TelemetrySample> m_buf;
    std::size_t                  m_head = 0;  // next write slot
    std::size_t                  m_size = 0;
};

// CSV: one header row then one row per sample, oldest -> newest. Floats use "%.9g" (exact float
// round-trip). Returns false on I/O failure.
void write_csv(const TelemetryRing& ring, std::ostream& os);
bool write_csv(const TelemetryRing& ring, const std::filesystem::path& path);

// Reads a file written by write_csv into `out` (cleared first; if the file has more rows than
// out.capacity() only the newest are kept). False on a missing/mismatched header or malformed row.
bool read_csv(std::istream& is, TelemetryRing& out);

// "build-debug/telemetry" (relative to the working directory).
std::filesystem::path telemetry_default_dir();
// Creates telemetry_default_dir() if needed and returns ".../telemetry_YYYYMMDD_HHMMSS.csv".
std::filesystem::path make_timestamped_csv_path();

}  // namespace swish
