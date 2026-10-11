#pragma once
// PassDeclaration: what a frame pass says about the named images (ImageRegistry `Img`) it reads and writes.
//
// Design note (S4, stub): this is DOCUMENTATION THAT IS CHECKED, not a render graph. Declarations never drive
// barriers, ordering or allocation (barriers stay eager, see IRenderPass.h; the list order in PassList.def stays the
// only truth). They exist so a debug-time validator (PassDeclarationValidator.h) can warn about a read-before-write in
// list order, and so the debug UI can show "who touches what". A pass that declares nothing (the default) is simply
// invisible to the validator. Plain data, no Vulkan call: Vulkan-free apart from the Img enum header.
//
// TODO(stub): per-subresource / layout-aware declarations, and "history" (previous-frame) reads, if this ever
// grows into automatic barrier placement. Today an image read and written by one pass (Hdr in the forward passes) is
// just listed in both vectors; the validator checks the read against EARLIER passes only.

#include "renderer/Passes/ImageRegistry.h"

#include <initializer_list>
#include <vector>

namespace swish {

struct PassDeclaration {
    std::vector<Img> reads;   // images sampled / loaded / blitted from
    std::vector<Img> writes;  // images rendered to / blitted to / stored

    PassDeclaration& read(std::initializer_list<Img> imgs) {
        reads.insert(reads.end(), imgs.begin(), imgs.end());
        return *this;
    }
    PassDeclaration& write(std::initializer_list<Img> imgs) {
        writes.insert(writes.end(), imgs.begin(), imgs.end());
        return *this;
    }
    bool empty() const { return reads.empty() && writes.empty(); }
};

}  // namespace swish
