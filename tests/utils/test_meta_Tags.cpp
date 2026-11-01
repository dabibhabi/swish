// utils/meta/Tags.h: tag types are empty, distinct and select an overload at compile time.
#include "utils/meta/Tags.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>

namespace tag = swish::tag;

namespace {
static_assert(std::is_empty_v<tag::flag_t> && std::is_empty_v<tag::real_t>);
static_assert(std::is_trivially_copyable_v<tag::flag_t> && std::is_trivially_copyable_v<tag::real_t>);
static_assert(!std::is_same_v<tag::flag_t, tag::real_t>);
static_assert(std::is_same_v<std::remove_const_t<decltype(tag::flag)>, tag::flag_t>);
static_assert(std::is_same_v<std::remove_const_t<decltype(tag::real)>, tag::real_t>);
static_assert(tag::is_tag_v<tag::flag_t> && tag::is_tag_v<const tag::real_t> && !tag::is_tag_v<int>);

// The pattern the tags exist for: one overload per kind, picked by the tag object.
std::string describe(tag::flag_t, bool v) { return v ? "flag:1" : "flag:0"; }
std::string describe(tag::real_t, float v) { return "real:" + std::to_string(static_cast<int>(v)); }

// A generic caller (like Tunables.h visit) forwards whichever tag it was handed.
template <class F>
std::string run(bool use_flag, F&& f) {
    return use_flag ? f(tag::flag, true) : f(tag::real, 7.0f);
}
}  // namespace

TEST_CASE("Tags: the tag object selects the overload", "[tags]") {
    CHECK(describe(tag::flag, true) == "flag:1");
    CHECK(describe(tag::flag, false) == "flag:0");
    CHECK(describe(tag::real, 3.0f) == "real:3");
}

TEST_CASE("Tags: a generic visitor dispatches on the forwarded tag", "[tags]") {
    auto visitor = [](auto kind, auto v) { return describe(kind, v); };
    CHECK(run(true, visitor) == "flag:1");
    CHECK(run(false, visitor) == "real:7");
}
