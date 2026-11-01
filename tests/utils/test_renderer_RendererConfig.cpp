// Chapter 01: prev_frame replaces the hand-written (f + 1) % 2 TAA history index.
#include "renderer/RendererConfig.h"

#include <catch2/catch_test_macros.hpp>

using namespace swish;

static_assert(prev_frame<2>(0) == 1 && prev_frame<2>(1) == 0);
static_assert(prev_frame<3>(0) == 2);

namespace {
template <u32 N>
void check_ring() {
    for (u32 f = 0; f < N; ++f) CHECK((prev_frame<N>(f) + 1) % N == f);
}
}  // namespace

TEST_CASE("prev_frame steps back one slot in the ring", "[renderer]") {
    check_ring<2>();
    check_ring<3>();
    check_ring<4>();
}

TEST_CASE("prev_frame matches the legacy TAA history index for two frames", "[renderer]") {
    for (u32 f = 0; f < 2; ++f) CHECK(prev_frame<2>(f) == (f + 1) % 2);
}
