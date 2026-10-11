#include "renderer/PipelineRebuild/PipelineRebuild.h"

#include <exception>
#include <utility>

namespace swish {

namespace {

// Run each callback once, in registration order; an exception is recorded and does not stop the rest.
void run_all(const std::vector<PipelineRebuild::Fn>& fns, const std::string& stem, std::size_t& ran,
             std::vector<std::string>* errors) {
    for (const PipelineRebuild::Fn& fn : fns) {
        ++ran;
        try {
            fn();
        } catch (const std::exception& e) {
            if (errors)
                errors->push_back(stem + ": " + e.what());
        } catch (...) {
            if (errors)
                errors->push_back(stem + ": unknown error");
        }
    }
}

}  // namespace

void PipelineRebuild::register_rebuild(std::string stem, Fn fn) {
    m_hooks[std::move(stem)].push_back(std::move(fn));
    ++m_count;
}

bool PipelineRebuild::has(std::string_view stem) const {
    return m_hooks.find(std::string(stem)) != m_hooks.end();
}

std::size_t PipelineRebuild::trigger(std::string_view stem, std::vector<std::string>* errors) const {
    std::size_t ran = 0;
    auto        it  = m_hooks.find(std::string(stem));
    if (it != m_hooks.end())
        run_all(it->second, it->first, ran, errors);
    return ran;
}

std::size_t PipelineRebuild::trigger_all(std::vector<std::string>* errors) const {
    std::size_t ran = 0;
    for (const auto& [stem, fns] : m_hooks)
        run_all(fns, stem, ran, errors);
    return ran;
}

}  // namespace swish
