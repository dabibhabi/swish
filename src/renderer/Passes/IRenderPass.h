#pragma once
// IRenderPass: one step of the frame. The PassRegistry (PassRegistry.h) holds an explicit, ordered list of them
// (PassList.def) and drives their lifecycle; the order of that list IS the command-buffer recording order.
//
// Types in this header:
//   IRenderPass   the lifecycle interface (class: it is a polymorphic component with invariants).
//   FrameCtx      what record() may read: the command buffer, indices, extents, named images, shared subsystems.
//   FrameUpdate   what beginFrame()/update() may read and write (CPU side, before recording).
//   FrameState    CPU values a pass publishes for later passes (wetness, adapted rain intensity, exposure).
//   PassResources non-owning pointers to Renderer-owned subsystems.
//   PassInitCtx   what init()/onResize() receive.
// The last five are plain data (struct): sub-components of the renderer, no invariants.
//
// Rules a pass follows (see docs/RECIPES.md "add a render pass"):
//  - Barriers are EAGER: the pass that leaves an image in a layout (or needs it in one) records the barrier
//    itself, at the same place in the command stream as before the registry existed. There is no tracker.
//  - Cross-frame history (TAA ping-pong images, previous view-projection, auto-exposure state) is private to the
//    pass that uses it.
//  - A pass never reaches into another pass; it reads named images (ImageRegistry) and FrameState.
//  - Release output must not change: debug-only passes are listed with PASS_DEBUG and compile to nothing otherwise.

#include "renderer/Passes/ImageRegistry.h"
#include "renderer/Passes/PassDeclaration.h"
#include "utils/types/Types.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace swish {

struct RendererServices;
class Camera;
class PostProcessManager;
class CameraUniforms;
class IBLManager;
class GeometryProducer;
class MaterialDescriptors;
class PassRegistry;
struct DebugParams;
#ifdef SWISH_DEBUG_UI
class DebugUI;
class SceneParamsUniform;
#endif

// CPU-side values a pass publishes for passes later in the list (a blackboard, not a service). Written in
// update(), read in record(). Defaults equal the pre-registry initial values.
struct FrameState {
    float wetness       = 0.0f;   // RainSystem integrated wetness [0,1]; feeds SSR, spray, windshield, the camera UBO
    float rainIntensity = 0.0f;   // RainSystem's current intensity; feeds the composite mist
    float exposure      = 0.45f;  // auto-exposure result (debug builds; composite reads it when tune.autoExposure)
};

// Non-owning handles to Renderer-owned subsystems passes may read. All valid from PassRegistry::init_all on.
struct PassResources {
    const PostProcessManager* pp             = nullptr;
    const CameraUniforms*     cameraUniforms = nullptr;
    const IBLManager*         ibl            = nullptr;
    const GeometryProducer*   geometry       = nullptr;
    MaterialDescriptors*      materials      = nullptr;  // G-buffer draws bind per-material sets
#ifdef SWISH_DEBUG_UI
    const SceneParamsUniform* sceneParams = nullptr;
    DebugUI*                  debugUI     = nullptr;  // records the overlay; begin_frame is the Renderer's
#endif
};

struct PassInitCtx {
    const RendererServices& services;
    ImageRegistry&          images;  // read inputs; register images this pass owns (Img::Velocity)
    const PassResources&    res;
    const PassRegistry&     passes;           // init-time lookup of an EARLIER pass (find<T>())
    VkExtent2D              renderExtent;     // SSAA render extent
    VkDescriptorSetLayout   cameraSetLayout;  // set 0 of every geometry-style pipeline
};

// Per-frame CPU inputs. `state` is the blackboard passes write; the other members are read-only.
struct FrameUpdate {
    uint32_t             frameIndex;
    float                dt;
    Camera*              camera;  // non-const: TaaPass sets the sub-pixel jitter in beginFrame
    const DebugParams&   tune;    // live DebugParams in debug builds, DebugParams{} defaults in release
    FrameState&          state;
    const PassResources& res;
    Vec3                 carVelocity;
    Vec3                 carPosition;
    float                rainIntensity;  // target intensity (R key / debug slider)
    bool                 wiperEnabled;
};

struct FrameCtx {
    VkCommandBuffer      cmd;
    uint32_t             frameIndex;
    uint32_t             imageIndex;
    VkExtent2D           renderExtent;  // SSAA: the whole offscreen chain
    VkExtent2D           swapExtent;    // composite output
    const ImageRegistry& images;
    const PassResources& res;
    const Camera*        camera;
    const FrameState&    state;
    const DebugParams&   tune;
    Vec3                 sunDir;
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    // Unique, stable name (labels, logs, tests, PassRegistry::find by name).
    virtual const char* name() const = 0;

    // Create GPU objects. Called once, in list order, after every Renderer-owned subsystem exists.
    virtual void init(const PassInitCtx& ctx) = 0;

    // Swapchain / SSAA-scale recreate: the PostProcessManager images are new and already re-registered.
    // Rebuild whatever depends on the render extent or on other images (framebuffers, descriptor writes).
    virtual void onResize(const PassInitCtx& ctx) { (void)ctx; }

    // CPU work before the camera UBO is written this frame (e.g. TaaPass camera jitter, auto-exposure readback).
    virtual void beginFrame(const FrameUpdate& u) { (void)u; }

    // CPU work after the camera UBO is written (uniform uploads, simulation steps, publishing FrameState).
    virtual void update(const FrameUpdate& u) { (void)u; }

    // Record commands. May do nothing (disabled / nothing to draw); when it does record, the images it touches must be
    // left in the layouts the NEXT pass in the list expects (documented in the pass and in docs/render-pipeline.md).
    virtual void record(const FrameCtx& ctx) = 0;

    // Declare the named images this pass reads / writes (PassDeclaration.h). Optional and purely informational: it
    // never changes recording. The default declares nothing. Checked by PassDeclarationValidator in debug builds.
    virtual void declare(PassDeclaration& d) const { (void)d; }

    // Destroy GPU objects (device is idle). Called in reverse list order.
    virtual void cleanup(VkDevice device) = 0;
};

}  // namespace swish
