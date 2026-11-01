// The explicit pass list: expands PassList.def into factory declarations and the registry builder.
#include "renderer/Passes/PassDeclarationValidator.h"
#include "renderer/Passes/PassRegistry.h"

namespace swish {

// ── factory declarations (each defined in the pass's own .cpp) ───────────────
#define PRODUCER(Name)
#define PASS(Name, Factory) std::unique_ptr<IRenderPass> Factory();
#ifdef SWISH_DEBUG_UI
#define PASS_DEBUG(Name, Factory) std::unique_ptr<IRenderPass> Factory();
#else
#define PASS_DEBUG(Name, Factory)
#endif
#include "renderer/Passes/PassList.def"
#undef PRODUCER
#undef PASS
#undef PASS_DEBUG

// ── the registry, in list order ──────────────────────────────────────────────
PassRegistry make_pass_registry(IRenderPass* geometry) {
    PassRegistry r;
#define PRODUCER(Name) \
    if (geometry) r.add_borrowed(geometry);
#define PASS(Name, Factory) r.add(Factory());
#ifdef SWISH_DEBUG_UI
#define PASS_DEBUG(Name, Factory) r.add(Factory());
#else
#define PASS_DEBUG(Name, Factory)
#endif
#include "renderer/Passes/PassList.def"
#undef PRODUCER
#undef PASS
#undef PASS_DEBUG
    (void)geometry;
    return r;
}

// ── the declarations, in list order (S4: PassDeclarationValidator input) ─────
// Same expansion as above, but each entry is queried through IRenderPass::declare on a throwaway instance (the
// constructors are GPU-free and init() is never called) because PassRegistry has no iteration accessor.
std::vector<NamedDeclaration> collect_pass_declarations(const IRenderPass* geometry) {
    std::vector<NamedDeclaration> out;
    auto                          add = [&out](const IRenderPass& p) {
        NamedDeclaration nd;
        nd.name = p.name();
        p.declare(nd.decl);
        out.push_back(std::move(nd));
    };
#define PRODUCER(Name) \
    if (geometry) add(*geometry);
#define PASS(Name, Factory) add(*Factory());
#ifdef SWISH_DEBUG_UI
#define PASS_DEBUG(Name, Factory) add(*Factory());
#else
#define PASS_DEBUG(Name, Factory)
#endif
#include "renderer/Passes/PassList.def"
#undef PRODUCER
#undef PASS
#undef PASS_DEBUG
    (void)geometry;
    return out;
}

}  // namespace swish
