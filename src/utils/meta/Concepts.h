#pragma once

// ── Concepts.h ───────────────────────────────────────────────────────
// Purpose   : named, reusable type predicates (C++20 concepts) shared across utils and the renderer: which enums may
//             index an array, which enums may be an FSM state / event / action, which structs may be copied as raw
//             bytes (push blocks, UBO mirrors, on-disk structs).
// Units     : none.
// Complexity: compile time only; emits no code.
// Naming    : namespace `swish::util::meta`. Specific concepts are built FROM general ones by name (IndexEnum from
//             CountedEnum, FsmState from IndexEnum) so subsumption orders overloads; never re-spell the traits.
// Example   : template <util::meta::TriviallyPackable T> void upload(const T&);
//             static_assert(util::meta::TriviallyPackable<RoadConfig>);
// See also  : EnumCycle.h (CountedEnum), docs/tutorials/typed-cpp/02-traits-and-concepts.md, utils/fsm/Fsm.h
// Test      : tests/utils/test_meta_Concepts.cpp
// Note      : std-only on purpose: RoadConfig.h includes this and is also compiled into tools/toml_baker.

#include "EnumCycle.h"

#include <type_traits>

namespace swish::util::meta {

// A scoped, counted enum with an unsigned underlying type: safe as an array index through std::to_underlying.
template <class E>
concept IndexEnum = CountedEnum<E> && std::is_unsigned_v<std::underlying_type_t<E>>;

template <class S>
concept FsmState = IndexEnum<S>;  // the states of a swish::util::fsm machine
template <class E>
concept FsmEvent = IndexEnum<E>;  // the event kinds a machine reacts to
template <class A>
concept FsmAction = std::is_scoped_enum_v<A>;  // what a transition asks its owner to do (no Count needed)

// Bytes can be copied as-is and field order is declaration order: push blocks, UBO mirrors, on-disk structs.
// (Not std::is_trivial: deprecated in C++26, and it does not imply standard layout.)
template <class T>
concept TriviallyPackable = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

}  // namespace swish::util::meta
