#pragma once
// PassRegistry: the explicit, ordered list of frame passes and their lifecycle driver.
//
// The registry holds factories' products only; it is not a service locator and has no global instance. The
// Renderer owns one, builds it with make_pass_registry() (PassList.cpp, from PassList.def) and calls
// init_all / resize_all / begin_frame / update / record_all / cleanup_all. Order never depends on static
// initialisation: it is the order of lines in PassList.def.

#include "renderer/Passes/IRenderPass.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace swish {

class PassRegistry {
public:
    // Append a pass the registry owns. Throws std::logic_error on a duplicate name.
    void add(std::unique_ptr<IRenderPass> pass);
    // Append a pass owned elsewhere (the geometry producer: the Renderer keeps its scene API). Same name rule.
    void add_borrowed(IRenderPass* pass);

    void init_all(const PassInitCtx& ctx);
    void resize_all(const PassInitCtx& ctx);
    void begin_frame(const FrameUpdate& u);
    void update(const FrameUpdate& u);
    void record_all(const FrameCtx& ctx) const;
    // Calls cleanup on every pass in reverse order, then drops the owned ones.
    void cleanup_all(VkDevice device);

    // First pass of dynamic type T, or nullptr. Init-time and test use; per-frame code reads FrameState instead.
    template <class T>
    T* find() const {
        for (const Entry& e : m_entries)
            if (auto* p = dynamic_cast<T*>(e.pass))
                return p;
        return nullptr;
    }

    std::size_t                   size() const { return m_entries.size(); }
    std::vector<std::string_view> names() const;

private:
    struct Entry {
        std::unique_ptr<IRenderPass> owned;  // null for borrowed passes
        IRenderPass*                 pass = nullptr;
    };
    void check_unique(const IRenderPass& pass) const;

    std::vector<Entry> m_entries;
};

// Builds the default registry from PassList.def. `geometry` is the producer entry (R4): the Renderer-owned object
// that draws the scene geometry into the shadow atlas and the G-buffer; null while it is not registered yet.
PassRegistry make_pass_registry(IRenderPass* geometry);

}  // namespace swish
