#pragma once
// PipelineRebuild: registry of "recreate my pipeline" callbacks keyed by shader stem ("composite" for
// composite.frag). ShaderHotReload calls trigger() after it recompiled a shader. GPU-free bookkeeping: the callbacks
// own the Vulkan work (they must be safe to run between frames, i.e. start with vkDeviceWaitIdle).
//
// Design note (S5, stub): several callbacks may share a stem (a .vert and .frag with the same stem both map here).
// A callback that throws is caught and reported as a failure; the others still run.
// TODO(stub): proper dependency tracking (shader -> pipelines that use it) instead of hand-registered stems.

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace swish {

class PipelineRebuild {
public:
    using Fn = std::function<void()>;

    // Register `fn` for `stem`. Several callbacks per stem are allowed (run in registration order).
    void register_rebuild(std::string stem, Fn fn);

    bool        has(std::string_view stem) const;
    std::size_t size() const { return m_count; }  // total callbacks

    // Run every callback registered for `stem`. Returns the number run; failures (exceptions) are appended to
    // `errors` as "stem: what()" when it is non-null. Zero when nothing is registered for the stem.
    std::size_t trigger(std::string_view stem, std::vector<std::string>* errors = nullptr) const;

    // Run every registered callback (a shaders/common include changed). Same return / errors contract.
    std::size_t trigger_all(std::vector<std::string>* errors = nullptr) const;

private:
    std::unordered_map<std::string, std::vector<Fn>> m_hooks;
    std::size_t                                      m_count = 0;
};

}  // namespace swish
