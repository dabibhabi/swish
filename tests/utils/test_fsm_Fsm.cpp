// utils/fsm/Fsm.h: the engine is specified by these tests. The file defines the traffic light of the tutorial
// (docs/tutorials/typed-cpp/04-fsm-engine.md) plus small deliberately broken tables for the validators.
#include "utils/fsm/Fsm.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>

namespace fsm = swish::util::fsm;
namespace um  = swish::util::meta;

namespace {
enum class Light : std::uint8_t { Red, Green, Yellow, Flashing, Count };
enum class LightEv : std::uint8_t { Timer, Fault, Reset, Count };
enum class LightAct : std::uint8_t { LampRed, LampGreen, LampYellow, StartBlink, StopBlink };

struct LightCtx {
    float green_s     = 0.0f;
    float min_green_s = 5.0f;
};
constexpr bool min_green_elapsed(const LightCtx& c) noexcept { return c.green_s >= c.min_green_s; }

using LightMachineSpecs = fsm::StateSpec<Light, LightAct>;
using LightRow          = fsm::Transition<Light, LightEv, LightCtx, LightAct>;

inline constexpr auto kTrafficLight = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red,
    {{.state = Light::Red, .on_entry = {LightAct::LampRed}},
     {.state = Light::Green, .on_entry = {LightAct::LampGreen}},
     {.state = Light::Yellow, .on_entry = {LightAct::LampYellow}},
     {.state = Light::Flashing, .on_entry = {LightAct::StartBlink}, .on_exit = {LightAct::StopBlink}}},
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green},
     {.from = Light::Green, .event = LightEv::Timer, .to = Light::Yellow, .guard = min_green_elapsed},
     {.from = Light::Yellow, .event = LightEv::Timer, .to = Light::Red},
     {.from = Light::Red, .event = LightEv::Fault, .to = Light::Flashing},
     {.from = Light::Green, .event = LightEv::Fault, .to = Light::Flashing},
     {.from = Light::Yellow, .event = LightEv::Fault, .to = Light::Flashing},
     {.from = Light::Flashing, .event = LightEv::Reset, .to = Light::Red}});

// The same specs for every variant table below.
#define LIGHT_STATES                                                                                         \
    {                                                                                                        \
        {.state = Light::Red, .on_entry = {LightAct::LampRed}},                                              \
            {.state = Light::Green, .on_entry = {LightAct::LampGreen}},                                      \
            {.state = Light::Yellow, .on_entry = {LightAct::LampYellow}}, {                                  \
            .state = Light::Flashing, .on_entry = {LightAct::StartBlink}, .on_exit = { LightAct::StopBlink } \
        }                                                                                                    \
    }

// An unguarded row followed by a same-key row: the second can never fire.
inline constexpr auto kShadowedTable = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green},
     {.from = Light::Red, .event = LightEv::Timer, .to = Light::Yellow}});

// Guarded then unguarded on one key: a legal priority list.
inline constexpr auto kPriorityTable = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green, .guard = min_green_elapsed},
     {.from = Light::Red, .event = LightEv::Timer, .to = Light::Yellow}});

// No row into Yellow.
inline constexpr auto kNoYellow = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green},
     {.from = Light::Green, .event = LightEv::Timer, .to = Light::Flashing},
     {.from = Light::Red, .event = LightEv::Fault, .to = Light::Flashing},
     {.from = Light::Flashing, .event = LightEv::Reset, .to = Light::Red}});

// No Reset row.
inline constexpr auto kNoReset = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green},
     {.from = Light::Green, .event = LightEv::Timer, .to = Light::Yellow},
     {.from = Light::Yellow, .event = LightEv::Timer, .to = Light::Flashing},
     {.from = Light::Flashing, .event = LightEv::Fault, .to = Light::Red}});

// Red listed twice, Flashing missing.
inline constexpr auto kBadStateList = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, {{.state = Light::Red}, {.state = Light::Red}, {.state = Light::Green}, {.state = Light::Yellow}},
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green}});

// An Internal row that moves.
inline constexpr auto kMovingInternal = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green, .kind = fsm::Kind::Internal}});

// Internal row with its own action; external self-transition on Flashing.
inline constexpr auto kInternalAndSelf = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from    = Light::Red,
      .event   = LightEv::Timer,
      .to      = Light::Red,
      .actions = {LightAct::LampYellow},
      .kind    = fsm::Kind::Internal},
     {.from = Light::Flashing, .event = LightEv::Fault, .to = Light::Flashing}});

// Two guarded rows on one key; ctx decides.
constexpr bool        always(const LightCtx&) noexcept { return true; }
constexpr bool        never(const LightCtx&) noexcept { return false; }
inline constexpr auto kTwoGuarded = fsm::make_machine<Light, LightEv, LightCtx, LightAct>(
    Light::Red, LIGHT_STATES,
    {{.from = Light::Red, .event = LightEv::Timer, .to = Light::Green, .guard = min_green_elapsed},
     {.from = Light::Red, .event = LightEv::Timer, .to = Light::Yellow, .guard = always}});

using LightActions = fsm::StepActions<LightAct>;

static_assert(fsm::validate(kTrafficLight).ok());
static_assert(fsm::step(kTrafficLight, Light::Red, LightEv::Timer, LightCtx{}).state == Light::Green);
static_assert(!fsm::step(kTrafficLight, Light::Red, LightEv::Reset, LightCtx{}).handled);
static_assert(!fsm::validate(kShadowedTable).no_shadowed_rows);
static_assert(std::is_trivially_copyable_v<fsm::StepResult<Light, LightAct>>);
static_assert(um::FsmState<Light> && um::FsmEvent<LightEv> && um::FsmAction<LightAct>);
}  // namespace

TEST_CASE("ActionList keeps order and capacity", "[fsm]") {
    fsm::ActionList<LightAct, 4> a{LightAct::LampRed, LightAct::LampGreen};
    CHECK(a.size() == 2);
    CHECK_FALSE(a.empty());
    a.push_back(LightAct::StartBlink);
    CHECK(a.size() == 3);
    CHECK(a[0] == LightAct::LampRed);
    CHECK(a[1] == LightAct::LampGreen);
    CHECK(a[2] == LightAct::StartBlink);
    std::size_t n = 0;
    for (LightAct x : a) {
        (void)x;
        ++n;
    }
    CHECK(n == 3);

    fsm::ActionList<LightAct, 4> b{LightAct::LampRed, LightAct::LampGreen, LightAct::StartBlink};
    CHECK(a == b);
    b.push_back(LightAct::StopBlink);
    CHECK_FALSE(a == b);
    CHECK(fsm::ActionList<LightAct, 4>{}.empty());

    // Equality ignores unused slots: same visible items after a different build history.
    fsm::ActionList<LightAct, 2> c{LightAct::LampRed};
    fsm::ActionList<LightAct, 2> d;
    d.push_back(LightAct::LampRed);
    CHECK(c == d);
    STATIC_REQUIRE((fsm::ActionList<LightAct, 2>{LightAct::LampRed}) ==
                   (fsm::ActionList<LightAct, 2>{LightAct::LampRed}));
}

TEST_CASE("step on an unguarded row moves and returns exit then row then entry actions", "[fsm]") {
    const auto r = fsm::step(kTrafficLight, Light::Flashing, LightEv::Reset, LightCtx{});
    CHECK(r.state == Light::Red);
    CHECK(r.handled);
    CHECK(r.actions == (LightActions{LightAct::StopBlink, LightAct::LampRed}));
}

TEST_CASE("step takes the first row whose guard passes", "[fsm]") {
    LightCtx ctx;
    ctx.green_s = 10.0f;  // min_green_elapsed true: the first row fires
    CHECK(fsm::step(kTwoGuarded, Light::Red, LightEv::Timer, ctx).state == Light::Green);
    ctx.green_s = 0.0f;  // first guard false: ordering falls through to the second
    CHECK(fsm::step(kTwoGuarded, Light::Red, LightEv::Timer, ctx).state == Light::Yellow);
    (void)never;
}

TEST_CASE("step with every guard false is unhandled", "[fsm]") {
    LightCtx ctx;
    ctx.green_s  = 1.0f;
    const auto r = fsm::step(kTrafficLight, Light::Green, LightEv::Timer, ctx);
    CHECK(r.state == Light::Green);
    CHECK_FALSE(r.handled);
    CHECK(r.actions.empty());
}

TEST_CASE("step on an event with no row is unhandled", "[fsm]") {
    const auto r = fsm::step(kTrafficLight, Light::Red, LightEv::Reset, LightCtx{});
    CHECK(r.state == Light::Red);
    CHECK_FALSE(r.handled);
    CHECK(r.actions.empty());
}

TEST_CASE("internal row skips exit and entry", "[fsm]") {
    const auto r = fsm::step(kInternalAndSelf, Light::Red, LightEv::Timer, LightCtx{});
    CHECK(r.handled);
    CHECK(r.state == Light::Red);
    CHECK(r.actions == (LightActions{LightAct::LampYellow}));  // no StopBlink / LampRed
}

TEST_CASE("external self transition runs exit then entry", "[fsm]") {
    const auto r = fsm::step(kInternalAndSelf, Light::Flashing, LightEv::Fault, LightCtx{});
    CHECK(r.handled);
    CHECK(r.state == Light::Flashing);
    CHECK(r.actions == (LightActions{LightAct::StopBlink, LightAct::StartBlink}));
}

TEST_CASE("enter returns the initial entry actions", "[fsm]") {
    CHECK(fsm::enter(kTrafficLight) == (fsm::RowActions<LightAct>{LightAct::LampRed}));
}

TEST_CASE("validate flags a shadowed duplicate row", "[fsm]") {
    CHECK_FALSE(fsm::validate(kShadowedTable).no_shadowed_rows);
    CHECK(fsm::validate(kPriorityTable).no_shadowed_rows);
}

TEST_CASE("validate flags an unreachable state", "[fsm]") {
    CHECK_FALSE(fsm::validate(kNoYellow).all_states_reachable);
    CHECK(fsm::validate(kTrafficLight).all_states_reachable);
}

TEST_CASE("validate flags an event no row handles", "[fsm]") {
    CHECK_FALSE(fsm::validate(kNoReset).all_events_handled);
    CHECK(fsm::validate(kTrafficLight).all_events_handled);
}

TEST_CASE("validate flags a bad state list or a moving internal row", "[fsm]") {
    CHECK_FALSE(fsm::validate(kBadStateList).states_listed_once);
    CHECK(fsm::validate(kTrafficLight).states_listed_once);
    CHECK_FALSE(fsm::validate(kMovingInternal).internal_rows_stay_put);
    CHECK(fsm::validate(kInternalAndSelf).internal_rows_stay_put);
    CHECK_FALSE(fsm::validate(kBadStateList).ok());
}

TEST_CASE("traffic light cycles at runtime like at compile time", "[fsm]") {
    LightCtx ctx;
    ctx.green_s            = 10.0f;  // guard satisfied
    Light       s          = Light::Red;
    const Light expected[] = {Light::Green, Light::Yellow, Light::Red};
    for (Light e : expected) {
        const auto r = fsm::step(kTrafficLight, s, LightEv::Timer, ctx);
        CHECK(r.handled);
        s = r.state;
        CHECK(s == e);
    }
}
