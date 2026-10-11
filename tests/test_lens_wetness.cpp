#include "renderer/WindshieldRainPass/LensWetness.h"

#include <catch2/catch_test_macros.hpp>

using namespace swish;

// GPU-free tests of the lamp-lens wetness model stub.

namespace {
float run(float rain, float speed, float seconds, LensWetnessModel& m, const char* lens = "Headlight_L_Lens") {
    for (float t = 0.0f; t < seconds; t += 0.05f)
        m.step(0.05f, rain, speed);
    return m.get(lens).coverage;
}
}  // namespace

TEST_CASE("Lens wetness: zero when dry", "[rain][lens]") {
    LensWetnessModel m;
    m.add_lens("Headlight_L_Lens");
    REQUIRE(run(0.0f, 0.0f, 30.0f, m) == 0.0f);
    REQUIRE(m.get("Headlight_L_Lens").dropSize == 0.0f);
    REQUIRE(m.get("Unknown").coverage == 0.0f);
}

TEST_CASE("Lens wetness: monotonic in rain", "[rain][lens]") {
    float prev = -1.0f;
    for (float rain : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        LensWetnessModel m;
        m.add_lens("Headlight_L_Lens");
        float c = run(rain, 0.0f, 3.0f, m);
        REQUIRE(c >= prev);
        prev = c;
    }
    REQUIRE(prev > 0.1f);
}

TEST_CASE("Lens wetness: cleared by airflow at speed", "[rain][lens]") {
    LensWetnessModel m;
    m.add_lens("TailLight_R_Lens");
    float parked = run(1.0f, 0.0f, 20.0f, m, "TailLight_R_Lens");
    REQUIRE(parked > 0.5f);
    LensWetnessModel slow, fast;
    slow.add_lens("a");
    fast.add_lens("a");
    float cSlow = run(1.0f, 15.0f, 20.0f, slow, "a");
    float cFast = run(1.0f, 40.0f, 20.0f, fast, "a");
    REQUIRE(cFast < cSlow);
    REQUIRE(cSlow < parked);
    REQUIRE(cFast < 0.2f);
    // Rain stops, car at speed: dries out.
    run(0.0f, 40.0f, 10.0f, m, "TailLight_R_Lens");
    REQUIRE(m.get("TailLight_R_Lens").coverage < 0.01f);
    REQUIRE(m.airflow_factor(0.0f) == 0.0f);
    REQUIRE(m.airflow_factor(100.0f) == 1.0f);
}

TEST_CASE("Lens wetness: drains over time when parked and rain stops", "[rain][lens]") {
    LensWetnessModel m;
    m.add_lens("Headlight_R_Lens");
    float wet = run(1.0f, 0.0f, 10.0f, m, "Headlight_R_Lens");
    float c1  = run(0.0f, 0.0f, 5.0f, m, "Headlight_R_Lens");
    float c2  = run(0.0f, 0.0f, 20.0f, m, "Headlight_R_Lens");
    REQUIRE(c1 < wet);
    REQUIRE(c2 < c1);
}

TEST_CASE("Lens wetness: lenses independent, GPU POD layout", "[rain][lens]") {
    LensWetnessModel m;
    m.add_lens("A");
    m.add_lens("B");
    m.step(1.0f, 1.0f, 0.0f);
    REQUIRE(m.get("A").coverage == m.get("B").coverage);
    LensWetnessGpu g = m.gpu("A");
    REQUIRE(g.coverage == m.get("A").coverage);
    STATIC_REQUIRE(sizeof(LensWetnessGpu) % 16 == 0);
    m.reset();
    REQUIRE(m.get("A").coverage == 0.0f);
}
