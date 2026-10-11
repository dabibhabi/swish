#include "TelemetryRing.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <istream>
#include <ostream>
#include <string>
#include <system_error>

namespace swish {

namespace {

constexpr const char* kHeader =
    "t,x,y,z,yaw_deg,speed_mps,gear,rpm,steer_deg,throttle,brake,mu,"
    "ws0,ws1,ws2,ws3,susp0,susp1,susp2,susp3,in_steer,in_throttle,in_brake,in_mode";
constexpr int kColumns = 24;

void put(std::string& line, float v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g,", static_cast<double>(v));
    line += buf;
}

bool parse_float(const std::string& tok, float& out) {
    if (tok.empty())
        return false;
    char* end = nullptr;
    out       = std::strtof(tok.c_str(), &end);
    return end == tok.c_str() + tok.size();
}

}  // namespace

void write_csv(const TelemetryRing& ring, std::ostream& os) {
    os << kHeader << '\n';
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const TelemetrySample& s = ring.at(i);
        std::string            line;
        put(line, s.t);
        put(line, s.x);
        put(line, s.y);
        put(line, s.z);
        put(line, s.yaw_deg);
        put(line, s.speed_mps);
        line += std::to_string(s.gear) + ",";
        put(line, s.rpm);
        put(line, s.steer_deg);
        put(line, s.throttle);
        put(line, s.brake);
        put(line, s.mu);
        for (float w : s.wheel_spin)
            put(line, w);
        for (float v : s.susp)
            put(line, v);
        put(line, s.input.steer);
        put(line, s.input.throttle);
        put(line, s.input.brake);
        line += std::to_string(static_cast<int>(s.input.steer_mode));
        os << line << '\n';
    }
}

bool write_csv(const TelemetryRing& ring, const std::filesystem::path& path) {
    std::ofstream f(path);
    if (!f)
        return false;
    write_csv(ring, f);
    return static_cast<bool>(f);
}

bool read_csv(std::istream& is, TelemetryRing& out) {
    out.clear();
    std::string line;
    if (!std::getline(is, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line != kHeader)
        return false;

    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        std::vector<std::string> tok;
        std::size_t              pos = 0;
        while (true) {
            const std::size_t c = line.find(',', pos);
            tok.push_back(line.substr(pos, c == std::string::npos ? std::string::npos : c - pos));
            if (c == std::string::npos)
                break;
            pos = c + 1;
        }
        if (static_cast<int>(tok.size()) != kColumns)
            return false;

        float v[kColumns]{};
        for (int i = 0; i < kColumns; ++i)
            if (!parse_float(tok[static_cast<std::size_t>(i)], v[i]))
                return false;

        TelemetrySample s;
        s.t         = v[0];
        s.x         = v[1];
        s.y         = v[2];
        s.z         = v[3];
        s.yaw_deg   = v[4];
        s.speed_mps = v[5];
        s.gear      = std::atoi(tok[6].c_str());
        s.rpm       = v[7];
        s.steer_deg = v[8];
        s.throttle  = v[9];
        s.brake     = v[10];
        s.mu        = v[11];
        for (int i = 0; i < 4; ++i) {
            s.wheel_spin[static_cast<std::size_t>(i)] = v[12 + i];
            s.susp[static_cast<std::size_t>(i)]       = v[16 + i];
        }
        s.input.steer      = v[20];
        s.input.throttle   = v[21];
        s.input.brake      = v[22];
        s.input.steer_mode = std::atoi(tok[23].c_str()) == 1 ? SteerMode::Rate : SteerMode::Target;
        out.push(s);
    }
    return true;
}

std::filesystem::path telemetry_default_dir() {
    return std::filesystem::path("build-debug") / "telemetry";
}

std::filesystem::path make_timestamped_csv_path() {
    const std::filesystem::path dir = telemetry_default_dir();
    std::error_code             ec;
    std::filesystem::create_directories(dir, ec);  // failure surfaces later as write_csv() == false
    const std::time_t now = std::time(nullptr);
    std::tm           tm{};
    localtime_r(&now, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "telemetry_%Y%m%d_%H%M%S.csv", &tm);
    return dir / stamp;
}

}  // namespace swish
