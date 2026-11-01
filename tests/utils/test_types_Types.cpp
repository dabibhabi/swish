// utils/types/*: the type vocabulary. Compile-time checks; the file must build without GLFW or Vulkan.
#include "utils/types/Types.h"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace {
using namespace swish;
static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8);
static_assert(sizeof(i8) == 1 && sizeof(i16) == 2 && sizeof(i32) == 4 && sizeof(i64) == 8);
static_assert(std::is_unsigned_v<u32> && std::is_signed_v<i32>);
static_assert(std::is_same_v<f32, float> && std::is_same_v<f64, double>);
static_assert(std::is_same_v<usize, std::size_t>);
static_assert(std::is_same_v<Vec3, glm::vec3> && std::is_same_v<vec3, glm::vec3>);
static_assert(std::is_same_v<Mat4, glm::mat4> && std::is_same_v<mat4, glm::mat4>);
static_assert(std::is_same_v<Quat, glm::quat>);
static_assert(std::is_same_v<glfw_window, ::GLFWwindow>);
}  // namespace

TEST_CASE("Types.h aliases resolve to the intended types", "[types]") {
    swish::Vec3 v{1.0f, 2.0f, 3.0f};
    CHECK(v == glm::vec3(1.0f, 2.0f, 3.0f));
    swish::u32 n = 7;
    CHECK(n == 7u);
    swish::glfw_window* w = nullptr;  // pointer to a forward-declared type: no GLFW include needed
    CHECK(w == nullptr);
}
