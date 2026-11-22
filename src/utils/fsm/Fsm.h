#pragma once

// ── Fsm.h ────────────────────────────────────────────────────────────
// Purpose   : a small table-driven finite state machine whose actions are returned AS DATA. `step` is a pure
//             constexpr function (state, event, context) -> (new state, handled, actions); the owner applies the
//             actions, so the machine never touches the car, cursor or renderer and is testable in static_assert.
// Units     : none.
// Complexity: step is O(N_T); validate is O(N_S * N_T); no allocation (fixed-capacity ActionList).
// Naming    : namespace `swish::util::fsm` (alias `namespace fsm = swish::util::fsm;`). States, events and actions
//             are scoped enums: S, E must satisfy meta::FsmState / FsmEvent (counted, unsigned), A meta::FsmAction.
// Semantics : 1 match = scan rows in declaration order; the first row with from == current, event == e and a guard
//             that is null or true fires (row order is priority).  2 external row = actions exit(from), row,
//             entry(to); a self-transition still runs exit and entry.  3 internal row = state unchanged, only the
//             row's actions, no exit/entry.  4 no match = {current, handled=false, no actions}.  5 the owner calls
//             enter(m) once for the initial entry actions. Flat machine: no superstates, no "from any" wildcard.
// Example   : enum class Light : u8 { Red, Green, Count };  enum class Ev : u8 { Timer, Count };
//             enum class Act { LampRed, LampGreen };
//             inline constexpr auto kLight = fsm::make_machine<Light, Ev, fsm::NoCtx, Act>(
//                 Light::Red,
//                 {{.state = Light::Red, .on_entry = {Act::LampRed}},
//                  {.state = Light::Green, .on_entry = {Act::LampGreen}}},
//                 {{.from = Light::Red, .event = Ev::Timer, .to = Light::Green},
//                  {.from = Light::Green, .event = Ev::Timer, .to = Light::Red}});
//             static_assert(fsm::validate(kLight).ok());
// See also  : docs/tutorials/typed-cpp/04-fsm-engine.md, utils/meta/Concepts.h
// Test      : tests/utils/test_fsm_Fsm.cpp
// Pitfalls  : guards must be capture-free and noexcept; tables in headers are `inline constexpr`; Machine has no
//             CTAD, use make_machine.

#include "utils/meta/Concepts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <utility>

namespace swish::util::fsm {

using meta::FsmAction;
using meta::FsmEvent;
using meta::FsmState;

struct NoCtx {};  // for machines without guards

template <class Ctx>
using Guard = bool (*)(const Ctx&) noexcept;

enum class Kind : std::uint8_t { External, Internal };

namespace detail {
// Not constexpr: reaching it during constant evaluation is a compile error; at runtime it is a loud failure.
[[noreturn]] inline void action_list_overflow() noexcept { std::abort(); }
}  // namespace detail

// Fixed-capacity, constexpr, allocation-free list of actions.
template <FsmAction A, std::size_t Cap>
class ActionList {
public:
    constexpr ActionList() noexcept = default;
    constexpr ActionList(std::initializer_list<A> items) noexcept {  // precondition: items.size() <= Cap
        for (A a : items) push_back(a);
    }

    constexpr void push_back(A a) noexcept {  // precondition: size() < Cap
        if (m_size >= Cap) detail::action_list_overflow();
        m_items[m_size++] = a;
    }
    template <std::size_t C2>
    constexpr void append(const ActionList<A, C2>& other) noexcept {
        for (A a : other) push_back(a);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return m_size == 0; }
    [[nodiscard]] constexpr const A*    begin() const noexcept { return m_items.data(); }
    [[nodiscard]] constexpr const A*    end() const noexcept { return m_items.data() + m_size; }
    [[nodiscard]] constexpr A           operator[](std::size_t i) const noexcept { return m_items[i]; }

    // Compares the first size() items only: unused slots do not matter.
    friend constexpr bool operator==(const ActionList& a, const ActionList& b) noexcept {
        if (a.m_size != b.m_size) return false;
        for (std::size_t i = 0; i < a.m_size; ++i)
            if (a.m_items[i] != b.m_items[i]) return false;
        return true;
    }

private:
    std::array<A, Cap> m_items{};
    std::size_t        m_size = 0;
};

inline constexpr std::size_t kMaxRowActions  = 2;
inline constexpr std::size_t kMaxStepActions = 3 * kMaxRowActions;  // exit + row + entry

template <FsmAction A>
using RowActions = ActionList<A, kMaxRowActions>;
template <FsmAction A>
using StepActions = ActionList<A, kMaxStepActions>;

template <FsmState S, FsmAction A>
struct StateSpec {
    S             state;
    RowActions<A> on_entry{};
    RowActions<A> on_exit{};
};

template <FsmState S, FsmEvent E, class Ctx, FsmAction A>
struct Transition {
    S             from;
    E             event;
    S             to;
    Guard<Ctx>    guard = nullptr;
    RowActions<A> actions{};
    Kind          kind = Kind::External;
};

template <FsmState S, FsmEvent E, class Ctx, FsmAction A, std::size_t NS, std::size_t NT>
struct Machine {
    S                                        initial;
    std::array<StateSpec<S, A>, NS>          states;
    std::array<Transition<S, E, Ctx, A>, NT> transitions;
};

template <FsmState S, FsmAction A>
struct StepResult {
    S              state;
    bool           handled = false;
    StepActions<A> actions{};
};

// Builds a Machine and deduces NS and NT from the two braced lists (S, E, Ctx, A are given explicitly).
template <FsmState S, FsmEvent E, class Ctx, FsmAction A, std::size_t NS, std::size_t NT>
[[nodiscard]] constexpr Machine<S, E, Ctx, A, NS, NT> make_machine(
    S initial, const StateSpec<S, A> (&states)[NS], const Transition<S, E, Ctx, A> (&rows)[NT]) noexcept {
    Machine<S, E, Ctx, A, NS, NT> m{.initial = initial, .states = {}, .transitions = {}};
    for (std::size_t i = 0; i < NS; ++i) m.states[i] = states[i];
    for (std::size_t i = 0; i < NT; ++i) m.transitions[i] = rows[i];
    return m;
}

// Every function below has the template head
//   template <FsmState S, FsmEvent E, class Ctx, FsmAction A, std::size_t NS, std::size_t NT>
// and takes `const Machine<S, E, Ctx, A, NS, NT>& m`.
#define SWISH_FSM_TPL template <FsmState S, FsmEvent E, class Ctx, FsmAction A, std::size_t NS, std::size_t NT>
#define SWISH_FSM_M const Machine<S, E, Ctx, A, NS, NT>&

namespace detail {
SWISH_FSM_TPL
[[nodiscard]] constexpr const StateSpec<S, A>* find_spec(SWISH_FSM_M m, S s) noexcept {
    for (const auto& spec : m.states)
        if (spec.state == s) return &spec;
    return nullptr;
}
}  // namespace detail

SWISH_FSM_TPL
[[nodiscard]] constexpr StepResult<S, A> step(SWISH_FSM_M m, S current, E event, const Ctx& ctx) noexcept {
    for (const auto& t : m.transitions) {
        if (t.from != current || t.event != event) continue;
        if (t.guard != nullptr && !t.guard(ctx)) continue;

        StepResult<S, A> r{.state = current, .handled = true, .actions = {}};
        if (t.kind == Kind::Internal) {
            r.actions.append(t.actions);
            return r;
        }
        r.state = t.to;
        if (const auto* s = detail::find_spec(m, current)) r.actions.append(s->on_exit);
        r.actions.append(t.actions);
        if (const auto* s = detail::find_spec(m, t.to)) r.actions.append(s->on_entry);
        return r;
    }
    return StepResult<S, A>{.state = current, .handled = false, .actions = {}};
}

// Initial entry actions: the owner calls this once at start.
SWISH_FSM_TPL
[[nodiscard]] constexpr RowActions<A> enter(SWISH_FSM_M m) noexcept {
    if (const auto* s = detail::find_spec(m, m.initial)) return s->on_entry;
    return {};
}

// ── Table validators (constexpr; static_assert(validate(table).ok())) ─────────────────────────

// Every value of S has exactly one StateSpec.
SWISH_FSM_TPL
[[nodiscard]] constexpr bool states_listed_once(SWISH_FSM_M m) noexcept {
    constexpr std::size_t n = meta::enum_count<S>();
    for (std::size_t v = 0; v < n; ++v) {
        std::size_t seen = 0;
        for (const auto& spec : m.states)
            if (std::to_underlying(spec.state) == v) ++seen;
        if (seen != 1) return false;
    }
    return NS == n;  // a spec for a value outside 0..Count-1 is also a bad list
}

// For rows i < j with the same (from, event), row i must have a guard: an unguarded row shadows every later one.
SWISH_FSM_TPL
[[nodiscard]] constexpr bool no_shadowed_rows(SWISH_FSM_M m) noexcept {
    for (std::size_t i = 0; i < NT; ++i)
        for (std::size_t j = i + 1; j < NT; ++j) {
            const auto& a = m.transitions[i];
            const auto& b = m.transitions[j];
            if (a.from == b.from && a.event == b.event && a.guard == nullptr) return false;
        }
    return true;
}

// R0 = {initial}; R(k+1) = Rk ∪ {t.to | t.from ∈ Rk}, ignoring guards. Every value of S must end up in it.
SWISH_FSM_TPL
[[nodiscard]] constexpr bool all_states_reachable(SWISH_FSM_M m) noexcept {
    constexpr std::size_t n = meta::enum_count<S>();
    std::array<bool, n>   seen{};
    seen[std::to_underlying(m.initial)] = true;
    for (std::size_t round = 0; round < n; ++round)
        for (const auto& t : m.transitions)
            if (seen[std::to_underlying(t.from)]) seen[std::to_underlying(t.to)] = true;
    for (bool b : seen)
        if (!b) return false;
    return true;
}

// Every value of E appears in at least one row (an event no row mentions is dead or a missing transition).
SWISH_FSM_TPL
[[nodiscard]] constexpr bool all_events_handled(SWISH_FSM_M m) noexcept {
    constexpr std::size_t n = meta::enum_count<E>();
    std::array<bool, n>   seen{};
    for (const auto& t : m.transitions) seen[std::to_underlying(t.event)] = true;
    for (bool b : seen)
        if (!b) return false;
    return true;
}

// An Internal row never changes state.
SWISH_FSM_TPL
[[nodiscard]] constexpr bool internal_rows_stay_put(SWISH_FSM_M m) noexcept {
    for (const auto& t : m.transitions)
        if (t.kind == Kind::Internal && t.to != t.from) return false;
    return true;
}

struct Report {
    bool                         states_listed_once     = false;
    bool                         no_shadowed_rows       = false;
    bool                         all_states_reachable   = false;
    bool                         all_events_handled     = false;
    bool                         internal_rows_stay_put = false;
    [[nodiscard]] constexpr bool ok() const noexcept {
        return states_listed_once && no_shadowed_rows && all_states_reachable && all_events_handled &&
               internal_rows_stay_put;
    }
};

SWISH_FSM_TPL
[[nodiscard]] constexpr Report validate(SWISH_FSM_M m) noexcept {
    return Report{.states_listed_once     = states_listed_once(m),
                  .no_shadowed_rows       = no_shadowed_rows(m),
                  .all_states_reachable   = all_states_reachable(m),
                  .all_events_handled     = all_events_handled(m),
                  .internal_rows_stay_put = internal_rows_stay_put(m)};
}

#undef SWISH_FSM_M
#undef SWISH_FSM_TPL

}  // namespace swish::util::fsm
