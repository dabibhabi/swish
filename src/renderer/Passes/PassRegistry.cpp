#include "PassRegistry.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace swish {

void PassRegistry::check_unique(const IRenderPass& pass) const {
    for (const Entry& e : m_entries)
        if (std::strcmp(e.pass->name(), pass.name()) == 0)
            throw std::logic_error(std::string("PassRegistry: duplicate pass name '") + pass.name() + "'");
}

void PassRegistry::add(std::unique_ptr<IRenderPass> pass) {
    check_unique(*pass);
    IRenderPass* raw = pass.get();
    m_entries.push_back({std::move(pass), raw});
}

void PassRegistry::add_borrowed(IRenderPass* pass) {
    check_unique(*pass);
    m_entries.push_back({nullptr, pass});
}

void PassRegistry::init_all(const PassInitCtx& ctx) {
    for (Entry& e : m_entries)
        e.pass->init(ctx);
}

void PassRegistry::resize_all(const PassInitCtx& ctx) {
    for (Entry& e : m_entries)
        e.pass->onResize(ctx);
}

void PassRegistry::begin_frame(const FrameUpdate& u) {
    for (Entry& e : m_entries)
        e.pass->beginFrame(u);
}

void PassRegistry::update(const FrameUpdate& u) {
    for (Entry& e : m_entries)
        e.pass->update(u);
}

void PassRegistry::record_all(const FrameCtx& ctx) const {
    for (const Entry& e : m_entries)
        e.pass->record(ctx);
}

void PassRegistry::cleanup_all(VkDevice device) {
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
        it->pass->cleanup(device);
    m_entries.clear();
}

std::vector<std::string_view> PassRegistry::names() const {
    std::vector<std::string_view> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries)
        out.emplace_back(e.pass->name());
    return out;
}

}  // namespace swish
