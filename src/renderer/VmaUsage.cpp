// The single translation unit that compiles the VulkanMemoryAllocator
// implementation. VMA is header-only: exactly one .cpp must define
// VMA_IMPLEMENTATION before including the header. Everyone else includes
// <vk_mem_alloc.h> for the declarations only.
//
// VMA's generated implementation trips a lot of -Wall/-Wextra/-Wpedantic
// warnings that are not our code to fix, so silence diagnostics for this TU.
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wall"
#    pragma GCC diagnostic ignored "-Wextra"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif
