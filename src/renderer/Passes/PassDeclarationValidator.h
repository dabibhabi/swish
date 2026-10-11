#pragma once
// PassDeclarationValidator: debug-time check over the ordered list of pass declarations (PassDeclaration.h).
//
// Design note (S4, stub): one free function walks the declarations in LIST ORDER and warns when a pass reads an image
// no earlier pass has written. Images that are legitimately filled outside the list are allowlisted (default
// list below): the AO images are primed white at start-up by PostProcessManager and, in release, no pass writes
// them; Lum is the auto-exposure history. It only reports: it never reorders passes and never changes behaviour.
// Vulkan-free (the Img enum header only), so it is unit-tested on synthetic data (tests/utils).
//
// collect_pass_declarations() builds the list from PassList.def (defined in PassList.cpp). It constructs throwaway
// pass objects through the same factories (constructors are GPU-free; init() is never called on them) because
// PassRegistry exposes no iteration accessor.
// TODO(stub): replace the throwaway construction with a PassRegistry::for_each(fn) accessor and have the registry
// hand out declarations directly; add write-after-write / dead-write warnings.

#include "renderer/Passes/PassDeclaration.h"

#include <span>
#include <string>
#include <vector>

namespace swish {

class IRenderPass;

struct NamedDeclaration {
    std::string     name;
    PassDeclaration decl;
};

// Short readable name of an image ("Hdr", "AoBlur", ...) for warnings and the UI.
const char* img_name(Img id);

// Images that are valid to read before any pass in the list wrote them (primed / cross-frame history).
std::span<const Img> default_primed_images();

// One warning per (pass, image) read-before-write, e.g. "Composite reads Ssr before any pass writes it".
// `primed` are exempt. A pass's own writes do not satisfy its own reads.
std::vector<std::string> validate_pass_declarations(const std::vector<NamedDeclaration>& ordered,
                                                    std::span<const Img> primed = default_primed_images());

// The declarations of the real pass list (PassList.def, same debug/release expansion as the registry), in order.
// `geometry` is the Renderer-owned producer entry (may be null: then it is skipped, like make_pass_registry).
std::vector<NamedDeclaration> collect_pass_declarations(const IRenderPass* geometry);

}  // namespace swish
