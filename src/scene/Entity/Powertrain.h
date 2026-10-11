#pragma once

#include "CarParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <tuple>

namespace swish {
using sz_t = std::size_t;
inline float engine_torque(float rpm) {
    struct Pt {
        float rpm, nm;
    };
    static constexpr Pt kCurve[] = {
        {900.f, 250.f},  {1500.f, 480.f}, {2000.f, 700.f},  {2500.f, 800.f}, {4000.f, 800.f},
        {5000.f, 760.f}, {6000.f, 715.f}, {6750.f, 676.2f}, {7000.f, 620.f}, {7200.f, 0.f},
    };
    constexpr float  kRedline = 7200.f;
    constexpr size_t kCurveN  = sizeof(kCurve) / sizeof(kCurve[0]);
    if (rpm >= kRedline)
        return 0.f;
    if (rpm <= kCurve[0].rpm)
        return kCurve[0].nm;
    for (size_t i = 0; i + 1 < kCurveN; ++i) {
        if (rpm >= kCurve[i].rpm && rpm <= kCurve[i + 1].rpm) {
            const float f = (rpm - kCurve[i].rpm) / (kCurve[i + 1].rpm - kCurve[i].rpm);
            return kCurve[i].nm + f * (kCurve[i + 1].nm - kCurve[i].nm);
        }
    }
    return 0.f;
}

struct GearboxParams {
    std::array<float, 7> ratios     = {5.20f, 3.13f, 2.10f, 1.54f, 1.19f, 0.97f, 0.82f};
    float                finalDrive = 3.44f;

    float upshiftRpm   = 6900.f;
    float downshiftRpm = 3200.f;
    float shiftDwell   = 0.3f;
};

// Chapter 01 stubs (exercise: replace each body with the real check).
inline constexpr sz_t kGearCount = std::tuple_size_v<decltype(GearboxParams::ratios)>;
static_assert(kGearCount == 7, "gear_for_speed (`i < 6`, `return 6`) and gearbox_shift_step (`state.gear < 6`) hard-code top gear 6: update them together with the ratio count");

[[nodiscard]] constexpr bool ratios_positive(const GearboxParams& gearbox) noexcept {
    if(!(gearbox.finalDrive > 0.f)) return false;
    for(const float m_ratio : gearbox.ratios) if(!(m_ratio > 0.f)) return false;
    return true;
}

[[nodiscard]] constexpr bool ratios_strictly_descending(const GearboxParams& gearbox) noexcept {
    for( sz_t i =1; i < gearbox.ratios.size(); ++i) {
        if(!(gearbox.ratios[i] < gearbox.ratios[i-1])) return false;
    }
    return true;
}

[[nodiscard]] constexpr bool shift_band_ordered(const GearboxParams& gearbox) noexcept {
    return gearbox.upshiftRpm > gearbox.downshiftRpm;
}
[[nodiscard]] constexpr bool dwell_non_negative(const GearboxParams& gearbox) noexcept {
    return gearbox.shiftDwell >= 0.f;
}
[[nodiscard]] constexpr bool upshift_lands_above_downshift(const GearboxParams& gearbox) noexcept {
    if(!ratios_positive(gearbox)) return false;
    for( sz_t i = 0; i+1 < gearbox.ratios.size(); i++)
        if(!(gearbox.upshiftRpm * gearbox.ratios[i+1] / gearbox.ratios[i] > gearbox.downshiftRpm)) return false;
    return true;

}
[[nodiscard]] constexpr bool downshift_lands_below_upshift(const GearboxParams& gearbox) noexcept {
    if(!ratios_positive(gearbox)) return false;

    for(sz_t i = 0; i+1 < gearbox.ratios.size(); i++) {
        float x = gearbox.ratios[i];
        float y = gearbox.ratios[i+1];
        float a = gearbox.downshiftRpm;
        float b = gearbox.upshiftRpm;

        if((a*x)/y >= b) return false;
    }
    return true;
}
[[nodiscard]] constexpr bool gearbox_params_valid(const GearboxParams& gearbox) noexcept {
    return ratios_positive(gearbox) && ratios_strictly_descending(gearbox) &&
            shift_band_ordered(gearbox) && dwell_non_negative(gearbox) &&
            upshift_lands_above_downshift(gearbox) && downshift_lands_below_upshift(gearbox);

}

inline float engine_rpm(float v, int gear, const GearboxParams& g, float rWheel) {
    const float vEff  = std::max(std::fabs(v), 3.0f);
    const float ratio = g.ratios[static_cast<size_t>(gear)] * g.finalDrive;
    return vEff * ratio * 60.0f / (2.0f * kEnginePi * rWheel);
}

inline float gear_drive_force(float v, int gear, const GearboxParams& g, const CarParams& p, float rWheel) {
    const float rpm   = engine_rpm(v, gear, g, rWheel);
    const float ratio = g.ratios[static_cast<size_t>(gear)] * g.finalDrive;
    return engine_torque(rpm) * ratio * p.drivetrainEff / rWheel;
}

inline int gear_for_speed(float v, const GearboxParams& g, float rWheel) {
    for (int i = 0; i < 6; ++i) {
        if (engine_rpm(v, i, g, rWheel) < g.upshiftRpm)
            return i;
    }
    return 6;
}

struct GearState {
    int   gear       = 0;
    float sinceShift = 1e9f;
};

struct GearEvent;
struct GearAction;
struct GearTransition;

inline GearState gearbox_shift_step(GearState state, float v, const GearboxParams& g, float rWheel, float dt) {
    state.sinceShift += dt;
    const float rpm = engine_rpm(v, state.gear, g, rWheel);
    if (state.sinceShift >= g.shiftDwell) {
        if (rpm >= g.upshiftRpm && state.gear < 6) {
            state.gear += 1;
            state.sinceShift = 0.0f;
        } else if (rpm <= g.downshiftRpm && state.gear > 0) {
            state.gear -= 1;
            state.sinceShift = 0.0f;
        }
    }
    return state;
}


static_assert(ratios_positive(GearboxParams{}), "gear ratios and finalDrive must be > 0");
static_assert(ratios_strictly_descending(GearboxParams{}), "gear ratios must strictly decrease from 1st to 7th");
static_assert(shift_band_ordered(GearboxParams{}), "upshiftRpm must be above downshiftRpm");
static_assert(dwell_non_negative(GearboxParams{}), "shiftDwell must be >= 0");
static_assert(upshift_lands_above_downshift(GearboxParams{}), "an upshift would land at or below downshiftRpm: the box would hunt");
static_assert(downshift_lands_below_upshift(GearboxParams{}), "a downshift would land at or above upshiftRpm: the box would hunt");


}  // namespace swish
