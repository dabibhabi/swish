// The explicit pass list: expands PassList.def into factory declarations and the registry builder.
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
    if (geometry)      \
        r.add_borrowed(geometry);
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

}  // namespace swish
