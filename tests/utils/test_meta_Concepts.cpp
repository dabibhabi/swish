// utils/meta/Concepts.h: IndexEnum / FsmState / FsmEvent / FsmAction / TriviallyPackable, plus the tightened
// CountedEnum. Most checks are static_assert; each TEST_CASE repeats them at runtime so ctest reports them.
#include "scene/CameraMode.h"
#include "scene/WeatherPreset.h"
#include "utils/meta/Concepts.h"
#include "utils/types/Types.h"
#include "utils/vulkan/DeviceHandle.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace um = swish::util::meta;

namespace {
enum class Ok : std::uint8_t { A, B, Count };
enum class Signed : std::int8_t { A, Count };
enum Plain { PA, Count };
struct HasCountMember {
    static constexpr int Count = 3;
};
enum class NoCount : std::uint8_t { A, B };
enum class Empty : std::uint8_t { Count = 0 };
enum class Action { Go, Stop };

struct Floats4 {
    float a, b, c, d;
};
struct EmptyTag {};
struct Virt {
    virtual ~Virt() = default;
};
class Mixed {
public:
    int a;

private:
    [[maybe_unused]] int b;
};
struct Sixteen {
    float v[4];
};
struct Eight {
    float v[2];
};

template <class T>
concept PushLike = um::TriviallyPackable<T> && sizeof(T) % 16 == 0;

constexpr int f(um::TriviallyPackable auto) { return 1; }
constexpr int f(PushLike auto) { return 2; }

static_assert(HasCountMember::Count == 3);
static_assert(um::IndexEnum<Ok> && um::FsmState<Ok> && um::FsmEvent<Ok> && um::CountedEnum<Ok>);
static_assert(um::CountedEnum<Signed> && !um::IndexEnum<Signed>);
static_assert(!um::CountedEnum<Plain> && !um::CountedEnum<HasCountMember> && !um::CountedEnum<int>);
static_assert(!um::CountedEnum<NoCount> && !um::CountedEnum<Empty>);
static_assert(um::FsmAction<Action> && um::FsmAction<Ok> && !um::FsmAction<Plain> && !um::FsmAction<int>);
static_assert(um::CountedEnum<swish::CameraMode> && um::CountedEnum<swish::WeatherPreset>);
static_assert(um::TriviallyPackable<Floats4> && um::TriviallyPackable<swish::Vec4> && um::TriviallyPackable<EmptyTag> &&
              um::TriviallyPackable<swish::vkx::DeviceRef>);
static_assert(!um::TriviallyPackable<Virt> && !um::TriviallyPackable<Mixed>);
static_assert(f(Sixteen{}) == 2 && f(Eight{}) == 1);
}  // namespace

TEST_CASE("IndexEnum accepts a counted scoped enum with an unsigned base", "[concepts]") {
    CHECK(um::IndexEnum<Ok>);
    CHECK(um::FsmState<Ok>);
    CHECK(um::FsmEvent<Ok>);
    CHECK(um::CountedEnum<Ok>);
}

TEST_CASE("IndexEnum rejects a signed base", "[concepts]") {
    CHECK(um::CountedEnum<Signed>);
    CHECK_FALSE(um::IndexEnum<Signed>);
}

TEST_CASE("CountedEnum rejects plain enums and non-enums", "[concepts]") {
    CHECK_FALSE(um::CountedEnum<Plain>);
    CHECK_FALSE(um::CountedEnum<HasCountMember>);
    CHECK_FALSE(um::CountedEnum<int>);
}

TEST_CASE("CountedEnum rejects a missing or empty Count", "[concepts]") {
    CHECK_FALSE(um::CountedEnum<NoCount>);
    CHECK_FALSE(um::CountedEnum<Empty>);
}

TEST_CASE("CameraMode and WeatherPreset still satisfy CountedEnum", "[concepts]") {
    CHECK(um::CountedEnum<swish::CameraMode>);
    CHECK(um::CountedEnum<swish::WeatherPreset>);
}

TEST_CASE("TriviallyPackable accepts POD mirrors and empty tags", "[concepts]") {
    CHECK(um::TriviallyPackable<Floats4>);
    CHECK(um::TriviallyPackable<swish::Vec4>);
    CHECK(um::TriviallyPackable<EmptyTag>);
    CHECK(um::TriviallyPackable<swish::vkx::DeviceRef>);
}

TEST_CASE("TriviallyPackable rejects virtual and mixed access types", "[concepts]") {
    CHECK_FALSE(um::TriviallyPackable<Virt>);
    CHECK_FALSE(um::TriviallyPackable<Mixed>);
}

TEST_CASE("A subsuming concept wins overload resolution", "[concepts]") {
    CHECK(f(Sixteen{}) == 2);
    CHECK(f(Eight{}) == 1);
}
