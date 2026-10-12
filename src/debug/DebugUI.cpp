#include "DebugUI.h"

// The whole translation unit is empty unless the debug UI is compiled in.
#ifdef SWISH_DEBUG_UI

#include "DebugParamsIO.h"
#include "Tunables.h"
#include "renderer/FrameCapture/FrameCapture.h"
#include "renderer/GpuProfiler/GpuProfiler.h"
#include "renderer/Passes/PassDeclarationValidator.h"
#include "renderer/ShaderHotReload/ShaderHotReload.h"
#include "scene/Entity/CarPhysics.h"  // WetGripParams — hydroplaning thresholds for the Telemetry panel
#include "utils/meta/Tags.h"
#include "utils/units/Units.h"
#include "utils/vulkan/Check.h"

// ImGuizmo.h uses ImVec4/ImGuiID and MUST follow imgui.h. `.clang-format`
// (IncludeBlocks: Regroup) would alphabetize "ImGuizmo" before "imgui" and break
// the build, so the exact off/on directives below pin the order across `make
// format` (the directive is only honored when the line is exactly this text).
// clang-format off
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "ImGuizmo.h"
// clang-format on

#include <array>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <string_view>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Init — ImGui context, GLFW + Vulkan backends, own pool/pass/framebuffers.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::init(const DebugUIInitInfo& info) {
    if (m_init)
        return;

    m_device = info.device;
    m_extent = info.extent;

    // ── Descriptor pool (owned) ───────────────────────────────────────
    // Sized generously per the classic ImGui example: 1000 of each of the
    // standard descriptor types. FREE_DESCRIPTOR_SET_BIT so ImGui may free
    // per-texture sets (used by ImGui_ImplVulkan_AddTexture) at will.
    {
        std::array<VkDescriptorPoolSize, 11> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 1000;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes    = poolSizes.data();
        VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));
    }

    createRenderPass(info.swapchainFormat);
    createFramebuffers(info.swapchainViews, info.extent);

    // ── ImGui context + backends ──────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // install_callbacks = true → chains ImGui's GLFW callbacks onto the ones
    // the app already installed (keyboard/mouse still reach the sim).
    ImGui_ImplGlfw_InitForVulkan(info.window, true);

    // v1.90+ takes a single ImGui_ImplVulkan_InitInfo* whose RenderPass is a
    // struct field (no separate RenderPass argument). Fonts upload lazily on
    // the first NewFrame in 1.91 — no manual font command buffer needed.
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance       = info.instance;
    initInfo.PhysicalDevice = info.physicalDevice;
    initInfo.Device         = info.device;
    initInfo.QueueFamily    = info.graphicsQueueFamily;
    initInfo.Queue          = info.graphicsQueue;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.RenderPass     = m_renderPass;
    initInfo.MinImageCount  = info.minImageCount;
    initInfo.ImageCount     = info.imageCount;
    initInfo.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);

    m_init = true;
}

// ══════════════════════════════════════════════════════════════════════
// Render pass — single color attachment, LOAD/STORE onto the swapchain
// image, PRESENT_SRC in and out (chains after the composite pass).
// ══════════════════════════════════════════════════════════════════════

void DebugUI::createRenderPass(VkFormat fmt) {
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = fmt;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;  // preserve the composited frame
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // composite left it here
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // ready to present

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // EXTERNAL → 0: wait for the composite pass's color writes (which end in
    // PRESENT_SRC) before we blend the UI on top.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    // loadOp LOAD reads the composited image, so the dependency must also cover color-attachment READs
    // (write-only here left the load unordered after the PRESENT_SRC -> COLOR_ATTACHMENT transition).
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &colorAtt;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass));
}

// ══════════════════════════════════════════════════════════════════════
// Framebuffers — one per swapchain image view, at the swap extent.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::createFramebuffers(const std::vector<VkImageView>& views, VkExtent2D extent) {
    m_extent = extent;
    m_framebuffers.resize(views.size());
    for (size_t i = 0; i < views.size(); i++) {
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_renderPass;
        info.attachmentCount = 1;
        info.pAttachments    = &views[i];
        info.width           = extent.width;
        info.height          = extent.height;
        info.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(m_device, &info, nullptr, &m_framebuffers[i]));
    }
}

void DebugUI::destroyFramebuffers() {
    for (auto& fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_framebuffers.clear();
}

// ══════════════════════════════════════════════════════════════════════
// Preset helpers — set groups of fields to canned "weather" looks.
// ══════════════════════════════════════════════════════════════════════

namespace {

void presetReset(DebugParams& p) {
    p           = DebugParams{};  // restore all defaults (keeps editMode/showPanel too)
    p.editMode  = true;
    p.showPanel = true;
}

// Flat grey overcast sky, low clarity, some fog, desaturated grade — the LIE
// (Long Island Expressway) reference look.
void presetOvercastLIE(DebugParams& p) {
    p.clarity       = 0.0f;
    p.rainIntensity = 0.0f;
    p.saturation    = 0.85f;
    p.contrast      = 1.0f;
    p.exposure      = 0.45f;
    p.fogColor      = glm::vec3(0.52f, 0.57f, 0.63f);
    p.fogDist63     = 900000.0f;
    p.fogMax        = 0.65f;
    p.sunAmbient    = 0.30f;
    p.sunColor      = glm::vec3(0.90f, 0.92f, 0.95f);  // cool, flat
}

// Bright blue clear day: full clarity, no fog, neutral grade.
void presetClear(DebugParams& p) {
    p.clarity       = 1.0f;
    p.rainIntensity = 0.0f;
    p.saturation    = 1.05f;
    p.contrast      = 1.05f;
    p.exposure      = 0.45f;
    p.fogMax        = 0.05f;
    p.fogDist63     = 4000000.0f;
    p.sunAmbient    = 0.22f;
    p.sunColor      = glm::vec3(1.0f, 0.95f, 0.85f);
}

// Overcast + heavy rain: clarity 0, rain 1, more fog, wetter surfaces.
void presetClearRain(DebugParams& p) {
    p.clarity       = 0.0f;
    p.rainIntensity = 1.0f;
    p.saturation    = 0.80f;
    p.fogColor      = glm::vec3(0.50f, 0.54f, 0.60f);
    p.fogDist63     = 700000.0f;
    p.fogMax        = 0.75f;
    p.wetPorosity   = 0.35f;
    p.wetRoughness  = 0.12f;
    p.sunAmbient    = 0.28f;
}

// Human-readable category for a MaterialId slot (for the material editor).
const char* materialName(int i) {
    switch (i) {
        case MAT_ASPHALT:
            return "asphalt";
        case MAT_GRASS:
            return "grass";
        case MAT_CONCRETE:
            return "concrete";
        case MAT_METAL:
            return "metal";
        case MAT_DEFAULT:
            return "default";
        case MAT_RUMBLE:
            return "rumble";
        case MAT_DIRT:
            return "dirt";
        case MAT_TREE:
            return "tree";
        default:
            break;
    }
    const auto u = static_cast<uint32_t>(i);  // MaterialId is uint32_t; avoids -Wsign-compare
    if (u >= MAT_SIGN_0 && u <= MAT_SIGN_7)
        return "sign";
    if (u >= MAT_CAR_0 && u <= MAT_CAR_LAST)
        return "car";
    return "?";
}

// Human-readable label for CarEntity::get_gear()'s 0-indexed gear (Telemetry panel).
const char* gearLabel(int gear) {
    static constexpr const char* kLabels[7] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th"};
    return (gear >= 0 && gear < 7) ? kLabels[gear] : "?";
}

// Per-kind ImGui widget and printf-dump text: one overload per value-kind tag (utils/meta/Tags.h). A new kind
// adds one `widget` and one `dump` here (plus put/get in DebugParamsIO.cpp); the group loops never change.
void widget(const tunables::Tunable& t, tag::flag_t, bool& v) {
    ImGui::Checkbox(t.label, &v);
}
void widget(const tunables::Tunable& t, tag::real_t, float& v) {
    ImGui::SliderFloat(t.label, &v, t.lo, t.hi, t.sliderFmt);
}
void dump(const tunables::Tunable&, tag::flag_t, bool v) {
    std::printf("%d", v ? 1 : 0);
}
void dump(const tunables::Tunable& t, tag::real_t, float v) {
    std::printf("%.*f", t.dumpPrec, static_cast<double>(v));
}

// Registry-driven controls / dump for one Tunables.def group (declaration order).
void draw_tunable_group(DebugParams& p, std::string_view group) {
    for (const tunables::Tunable& t : tunables::all()) {
        if (group != t.group)
            continue;
        tunables::visit(t, p, [&](auto kind, auto& v) { widget(t, kind, v); });
    }
}

void dump_tunable_group(const DebugParams& p, std::string_view group) {
    bool first = true;
    for (const tunables::Tunable& t : tunables::all()) {
        if (group != t.group)
            continue;
        std::printf("%s%s=", first ? "" : " ", t.name);
        tunables::visit(t, p, [&](auto kind, auto v) { dump(t, kind, v); });
        first = false;
    }
    std::printf("\n");
}

void printValues(const DebugParams& p) {
    std::printf("═══ DebugParams ═══\n");
    std::printf("exposure=%.4f bloomThreshold=%.4f bloomIntensity=%.4f\n", p.exposure, p.bloomThreshold,
                p.bloomIntensity);
    std::printf("brightness=%.4f contrast=%.4f saturation=%.4f temperature=%.4f tint=%.4f\n", p.brightness, p.contrast,
                p.saturation, p.temperature, p.tint);
    std::printf("autoExposure=%d aeKey=%.3f aeSpeed=%.2f aeMin=%.3f aeMax=%.3f\n", p.autoExposure ? 1 : 0, p.aeKey,
                p.aeSpeed, p.aeMin, p.aeMax);
    std::printf("skyHorizonOvercast={%.3f,%.3f,%.3f} skyHorizonClear={%.3f,%.3f,%.3f}\n", p.skyHorizonOvercast.x,
                p.skyHorizonOvercast.y, p.skyHorizonOvercast.z, p.skyHorizonClear.x, p.skyHorizonClear.y,
                p.skyHorizonClear.z);
    std::printf("skyZenithOvercast={%.3f,%.3f,%.3f} skyZenithClear={%.3f,%.3f,%.3f}\n", p.skyZenithOvercast.x,
                p.skyZenithOvercast.y, p.skyZenithOvercast.z, p.skyZenithClear.x, p.skyZenithClear.y,
                p.skyZenithClear.z);
    std::printf("clarity=%.4f sunDiscExp[%.1f..%.1f] sunDiscStr[%.2f..%.2f]\n", p.clarity, p.sunDiscExpMin,
                p.sunDiscExpMax, p.sunDiscStrMin, p.sunDiscStrMax);
    std::printf("sunColor={%.3f,%.3f,%.3f} sunAmbient=%.3f sunAzimuth=%.4f sunElevation=%.4f\n", p.sunColor.x,
                p.sunColor.y, p.sunColor.z, p.sunAmbient, p.sunAzimuth, p.sunElevation);
    std::printf("fogColor={%.3f,%.3f,%.3f} fogDist63=%.1f fogMax=%.3f\n", p.fogColor.x, p.fogColor.y, p.fogColor.z,
                p.fogDist63, p.fogMax);
    dump_tunable_group(p, "reflection");
    std::printf("ssaoEnabled=%d ssaoRadius=%.1f ssaoBias=%.1f ssaoIntensity=%.3f\n", p.ssaoEnabled ? 1 : 0,
                p.ssaoRadius, p.ssaoBias, p.ssaoIntensity);
    dump_tunable_group(p, "ssr");
    std::printf("steerAxisEdit=%d steerEuler={%.2f,%.2f,%.2f} steerQuat={%.4f,%.4f,%.4f,%.4f}\n",
                p.steerAxisEdit ? 1 : 0, p.steerEuler.x, p.steerEuler.y, p.steerEuler.z, p.steerQuat.x, p.steerQuat.y,
                p.steerQuat.z, p.steerQuat.w);
    std::printf("shadowBias=%.5f shadowFloor=%.3f lightSize=%.2f csmShadowFar=%.1f csmLambda=%.3f\n", p.shadowBias,
                p.shadowFloor, p.shadowLightSize, p.csmShadowFar, p.csmLambda);
    std::printf("depthBiasConst=%.3f depthBiasSlope=%.3f\n", p.depthBiasConst, p.depthBiasSlope);
    std::printf("rainIntensity=%.3f streakLen=%.1f\n", p.rainIntensity, p.streakLen);
    dump_tunable_group(p, "wet");
    dump_tunable_group(p, "car_lights");
    dump_tunable_group(p, "car");
    std::printf("carOverride=%d carMetalness=%.3f carPaint={%.3f,%.3f,%.3f} carRoughnessMul=%.3f\n",
                p.carOverride ? 1 : 0, p.carMetalness, p.carPaint.x, p.carPaint.y, p.carPaint.z, p.carRoughnessMul);
    std::printf("ssaaScale=%.3f\n", p.ssaaScale);
    std::fflush(stdout);
}

// "Render tools" section: GPU pass timings, frame capture, shader hot-reload, pass-declaration warnings.
// Display + two action callbacks only; owns no state.
void draw_render_tools(const RenderToolsView& t) {
    if (t.gpu) {
        ImGui::SeparatorText("GPU profiler");
        const GpuProfileSnapshot& s = *t.gpu;
        if (!s.supported) {
            ImGui::TextDisabled("GPU timestamps unsupported on this device/queue");
        } else if (s.frames == 0) {
            ImGui::TextDisabled("waiting for the first read-back...");
        } else {
            if (ImGui::BeginTable("gpuPasses", 3,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pass");
                ImGui::TableSetupColumn("smoothed ms");
                ImGui::TableSetupColumn("last ms");
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < s.names.size() && i < s.smoothedMs.size() && i < s.ms.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s.names[i].c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", s.smoothedMs[i]);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", s.ms[i]);
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Total");
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", s.smoothedTotalMs);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", s.totalMs);
                ImGui::EndTable();
            }
        }
    }

    if (t.capture || t.requestCapture) {
        ImGui::SeparatorText("Frame capture");
        const bool busy = t.capture && (t.capture->state == CaptureStatus::State::Requested ||
                                        t.capture->state == CaptureStatus::State::InFlight);
        ImGui::BeginDisabled(busy || !t.requestCapture);
        if (ImGui::Button("Capture LDR (F12)") && t.requestCapture)
            t.requestCapture(false);
        ImGui::SameLine();
        if (ImGui::Button("Capture HDR (Shift+F12)") && t.requestCapture)
            t.requestCapture(true);
        ImGui::EndDisabled();
        if (t.capture) {
            const char* state = "idle";
            switch (t.capture->state) {
                case CaptureStatus::State::Idle:
                    state = "idle";
                    break;
                case CaptureStatus::State::Requested:
                    state = "requested";
                    break;
                case CaptureStatus::State::InFlight:
                    state = "in flight";
                    break;
                case CaptureStatus::State::Saved:
                    state = "saved";
                    break;
                case CaptureStatus::State::Failed:
                    state = "FAILED";
                    break;
            }
            ImGui::Text("State: %s (%u saved)", state, t.capture->savedCount);
            if (!t.capture->message.empty())
                ImGui::TextWrapped("%s", t.capture->message.c_str());
            if (!t.capture->lastPath.empty())
                ImGui::TextWrapped("Last LDR: %s", t.capture->lastPath.c_str());
            if (!t.capture->lastHdrPath.empty())
                ImGui::TextWrapped("Last HDR: %s", t.capture->lastHdrPath.c_str());
        }
    }

    if (t.hotReload) {
        ImGui::SeparatorText("Shader hot-reload");
        bool enabled = t.hotReload->enabled();
        if (ImGui::Checkbox("Enabled##hotreload", &enabled) && t.setHotReloadEnabled)
            t.setHotReloadEnabled(enabled);
        const ShaderHotReload::Status& st = t.hotReload->status();
        ImGui::Text("Reloads: %u  failures: %u  rebuilds: %u  need restart: %u", st.reloads, st.compileFailures,
                    st.rebuilds, st.unhooked);
        if (!st.lastMessage.empty()) {
            if (st.lastOk)
                ImGui::TextWrapped("%s", st.lastMessage.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", st.lastMessage.c_str());
        }
    }

    if (t.declWarnings) {
        ImGui::SeparatorText("Pass declarations");
        if (t.decls)
            ImGui::TextDisabled("%zu passes checked", t.decls->size());
        if (t.declWarnings->empty()) {
            ImGui::TextDisabled("no warnings");
        } else {
            for (const std::string& w : *t.declWarnings)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", w.c_str());
        }
    }
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════
// begin_frame — new frame, build the panel (mutates p), then render.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::begin_frame(DebugParams& p, const Mat4& view, const Mat4& proj) {
    if (!m_init)
        return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // In drive-mode (!editMode) the cursor is locked to steer the car; tell
    // ImGui to ignore the mouse so it doesn't fight the grab. In edit-mode the
    // cursor is free and the panel is interactive.
    ImGuiIO& io = ImGui::GetIO();
    if (!p.editMode)
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    using util::units::kPi;  // == the literal 3.14159265358979323846f (static_assert in Units.h)

    if (p.showPanel) {
        ImGui::Begin("swish - debug");

        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::Checkbox("Edit mode (free cursor)", &p.editMode);

        // ── Telemetry (speed / gear / rpm / grip HUD — issue 5's readout) ──
        // The cockpit instrument cluster is BAKED PIXELS in the GLB texture
        // atlas (verified: zero gauge/needle/dial nodes across 3341 nodes —
        // the needle is painted at zero and can never move), so it cannot be
        // driven as a speed readout; this panel is the only one available.
        // Read-only: telemetry* fields are pushed once per frame from
        // CarEntity's live state (App.cpp, mirroring the steerPivotWorld /
        // cull-tally pattern used elsewhere in DebugParams) — this section
        // only DISPLAYS them, no tuning here.
        if (ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show HUD", &p.showTelemetry);
            if (p.showTelemetry) {
                const float mph = p.telemetrySpeedMps * 2.23694f;  // m/s -> mph
                const float kmh = p.telemetrySpeedMps * 3.6f;      // m/s -> km/h

                // Large/prominent speed readout (temporarily scale the font up).
                ImGui::SetWindowFontScale(1.6f);
                ImGui::Text("%.0f mph", mph);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::SameLine();
                ImGui::TextDisabled("(%.0f km/h)", kmh);

                ImGui::Separator();
                ImGui::Text("Gear: %s", gearLabel(p.telemetryGear));

                // RPM vs the active profile's redline (CarProfile torque curve: WOT torque is EXACTLY 0
                // there; 7200 for the 992 Turbo S, 6800 for the 993 RS). Progress bar colored
                // green -> amber -> red as it nears the limiter.
                const float kRedlineRpm = p.telemetryRedline;
                const float     rpmFrac     = glm::clamp(p.telemetryRpm / kRedlineRpm, 0.0f, 1.0f);
                const ImVec4    rpmColor    = rpmFrac > 0.9f   ? ImVec4(0.90f, 0.15f, 0.15f, 1.0f)   // red
                                              : rpmFrac > 0.7f ? ImVec4(0.95f, 0.75f, 0.10f, 1.0f)   // amber
                                                               : ImVec4(0.20f, 0.75f, 0.30f, 1.0f);  // green
                char            rpmLabel[32];
                std::snprintf(rpmLabel, sizeof(rpmLabel), "%.0f / %.0f rpm", p.telemetryRpm, kRedlineRpm);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rpmColor);
                ImGui::ProgressBar(rpmFrac, ImVec2(-1.0f, 0.0f), rpmLabel);
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Text("Effective mu: %.3f", p.telemetryMu);

                // Hydroplaning indicator. Thresholds anchor on WetGripParams
                // (CarPhysics.h): muDry=1.10 (dry asphalt), muWet=0.60 (low-speed
                // wet asphalt), muPlane=0.08 (full hydroplane).
                //   DRY          mu >= muDry - 0.02   — effective_mu()'s hard
                //                                        invariant returns EXACTLY
                //                                        muDry when wetness <= 0,
                //                                        so a tiny epsilon absorbs
                //                                        float noise without
                //                                        masking any real wetness.
                //   HYDROPLANING mu <= (muWet+muPlane)/2 (~0.34) — grip has
                //                                        collapsed more than
                //                                        halfway from wet-grip
                //                                        toward full plane: a
                //                                        clear, not borderline,
                //                                        plane rather than just
                //                                        ordinary wet fade.
                //   WET          everything between the two above.
                constexpr WetGripParams kGrip{};
                const float             kMuDryEps  = kGrip.muDry - 0.02f;                   // ~1.08
                const float             kMuPlaneTh = (kGrip.muWet + kGrip.muPlane) * 0.5f;  // ~0.34
                const char*             state;
                ImVec4                  stateColor;
                if (p.telemetryMu <= kMuPlaneTh) {
                    state      = "HYDROPLANING";
                    stateColor = ImVec4(0.95f, 0.15f, 0.15f, 1.0f);  // red
                } else if (p.telemetryMu < kMuDryEps) {
                    state      = "WET";
                    stateColor = ImVec4(0.95f, 0.75f, 0.10f, 1.0f);  // amber
                } else {
                    state      = "DRY";
                    stateColor = ImVec4(0.30f, 0.85f, 0.40f, 1.0f);  // green
                }
                ImGui::TextColored(stateColor, "%s", state);

                // Per-wheel slip (SlipVisuals.h; ratio is a force-utilisation proxy, angle is per-axle for now).
                ImGui::Separator();
                static constexpr const char* kWheelLabels[4] = {"FL", "FR", "BL", "BR"};
                if (ImGui::BeginTable("##wheelslip", 5, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Wheel");
                    ImGui::TableSetupColumn("Slip ratio");
                    ImGui::TableSetupColumn("Slip angle");
                    ImGui::TableSetupColumn("Spray x");
                    ImGui::TableSetupColumn("Skid");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < 4; ++i) {
                        const DebugParams::TelemetryWheel& w = p.telemetryWheel[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(kWheelLabels[i]);
                        ImGui::TableNextColumn();
                        ImGui::Text("%+.3f", static_cast<double>(w.slipRatio));
                        ImGui::TableNextColumn();
                        ImGui::Text("%+.1f deg", static_cast<double>(glm::degrees(w.slipAngle)));
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f", static_cast<double>(w.sprayBoost));
                        ImGui::TableNextColumn();
                        if (w.skid)
                            ImGui::TextColored(ImVec4(0.95f, 0.15f, 0.15f, 1.0f), "SKID");
                        else
                            ImGui::TextDisabled("-");
                    }
                    ImGui::EndTable();
                }

                // Lamp-lens wetness (LensWetness.h; CPU model only, not uploaded to the renderer yet).
                static constexpr const char* kLensLabels[4] = {"Headlight L", "Headlight R", "Tail L", "Tail R"};
                if (ImGui::BeginTable("##lenswet", 4, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Lens");
                    ImGui::TableSetupColumn("Coverage");
                    ImGui::TableSetupColumn("Drop size");
                    ImGui::TableSetupColumn("Streak");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < 4; ++i) {
                        const DebugParams::TelemetryLens& l = p.telemetryLens[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(kLensLabels[i]);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", static_cast<double>(l.coverage));
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", static_cast<double>(l.dropSize));
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", static_cast<double>(l.streak));
                    }
                    ImGui::EndTable();
                }
            }
        }

        // ── Driving tools (input source, record / replay / CSV, FFB readout) ──
        // Pure view/controls over the drv* fields in DebugParams; App (AppDrivingTools.cpp) acts on
        // them and writes the readouts. Defaults (Keyboard, nothing recording) change no behaviour.
        if (ImGui::CollapsingHeader("Driving tools")) {
            ImGui::SeparatorText("Input");
            ImGui::RadioButton("Keyboard", &p.drvInputSource, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Gamepad", &p.drvInputSource, 1);
            if (p.drvGamepadConnected)
                ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.40f, 1.0f), "Gamepad: %s", p.drvGamepadName);
            else
                ImGui::TextDisabled("Gamepad: none detected");
            ImGui::SliderFloat("Steer deadzone", &p.drvDeadzone, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("Steer expo", &p.drvExpo, 0.0f, 1.0f, "%.3f");
            ImGui::ProgressBar(0.5f + 0.5f * p.drvAxisSteer, ImVec2(-1.0f, 0.0f), "steer");
            ImGui::ProgressBar(p.drvAxisThrottle, ImVec2(-1.0f, 0.0f), "throttle");
            ImGui::ProgressBar(p.drvAxisBrake, ImVec2(-1.0f, 0.0f), "brake");
            ImGui::Text("steer %+.3f  throttle %.3f  brake %.3f", p.drvAxisSteer, p.drvAxisThrottle, p.drvAxisBrake);

            ImGui::SeparatorText("Telemetry recorder");
            ImGui::Checkbox("Record (new clip on enable)", &p.drvRecord);
            ImGui::Checkbox("Replay recorded inputs", &p.drvReplay);
            const float ringFrac =
                p.drvRingCapacity > 0 ? static_cast<float>(p.drvRingSize) / static_cast<float>(p.drvRingCapacity) : 0.f;
            char ringLabel[48];
            std::snprintf(ringLabel, sizeof(ringLabel), "ring %d / %d", p.drvRingSize, p.drvRingCapacity);
            ImGui::ProgressBar(ringFrac, ImVec2(-1.0f, 0.0f), ringLabel);
            if (p.drvReplay && p.drvReplaySize > 0) {
                char replayLabel[48];
                std::snprintf(replayLabel, sizeof(replayLabel), "replay %d / %d", p.drvReplayPos, p.drvReplaySize);
                ImGui::ProgressBar(static_cast<float>(p.drvReplayPos) / static_cast<float>(p.drvReplaySize),
                                   ImVec2(-1.0f, 0.0f), replayLabel);
            }
            if (ImGui::Button("Export CSV"))
                p.drvExportRequested = true;
            if (p.drvStatus[0])
                ImGui::TextDisabled("%s", p.drvStatus);

            ImGui::SeparatorText("Force feedback (readout only)");
            ImGui::Text("SAT target: %+.2f N*m  (normalized %+.3f)", p.drvFfbTorqueNm, p.drvFfbNormalized);
            ImGui::ProgressBar(0.5f + 0.5f * p.drvFfbNormalized, ImVec2(-1.0f, 0.0f), "");
            ImGui::TextDisabled("No device output: the target is computed, not sent.");
        }

        // ── Preset buttons row ────────────────────────────────────────
        if (ImGui::Button("Reset"))
            presetReset(p);
        ImGui::SameLine();
        if (ImGui::Button("Overcast-LIE"))
            presetOvercastLIE(p);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            presetClear(p);
        ImGui::SameLine();
        if (ImGui::Button("Clear+Rain"))
            presetClearRain(p);
        ImGui::SameLine();
        if (ImGui::Button("Print values"))
            printValues(p);

        // ── Named disk presets (toml save/load) ───────────────────────
        // Persist the current look to <presets_dir>/<name>.toml (per-user, outside build/;
        // see DebugParamsIO.h) and reload it later — or pick an existing preset from the combo to load it live.
        {
            static char                     nameBuf[64] = "lie";
            static std::vector<std::string> onDisk      = debugio::list_presets();
            static char                     status[96]  = "";

            ImGui::InputText("Preset name", nameBuf, sizeof(nameBuf));
            if (ImGui::Button("Save")) {
                bool ok = debugio::save(p, nameBuf);
                std::snprintf(status, sizeof(status), ok ? "saved '%s.toml'" : "SAVE FAILED '%s'", nameBuf);
                onDisk = debugio::list_presets();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                bool ok = debugio::load(p, nameBuf);
                std::snprintf(status, sizeof(status), ok ? "loaded '%s.toml'" : "not found '%s'", nameBuf);
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
                onDisk = debugio::list_presets();

            if (!onDisk.empty() && ImGui::BeginCombo("On disk", nameBuf)) {
                for (const std::string& n : onDisk) {
                    if (ImGui::Selectable(n.c_str(), n == nameBuf)) {
                        std::snprintf(nameBuf, sizeof(nameBuf), "%s", n.c_str());
                        debugio::load(p, n);
                        std::snprintf(status, sizeof(status), "loaded '%s.toml'", n.c_str());
                    }
                }
                ImGui::EndCombo();
            }
            if (status[0])
                ImGui::TextDisabled("%s", status);
        }

        // ── Image / Grade ─────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Image / Grade", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto-exposure", &p.autoExposure);
            if (p.autoExposure) {
                ImGui::SliderFloat("AE key (grey)", &p.aeKey, 0.05f, 1.0f);
                ImGui::SliderFloat("AE speed", &p.aeSpeed, 0.1f, 8.0f);
                ImGui::SliderFloat("AE min", &p.aeMin, 0.01f, 1.0f);
                ImGui::SliderFloat("AE max", &p.aeMax, 0.5f, 4.0f);
            } else {
                ImGui::SliderFloat("Exposure", &p.exposure, 0.0f, 2.0f);
            }
            ImGui::SliderFloat("Brightness", &p.brightness, -1.0f, 1.0f);
            ImGui::SliderFloat("Contrast", &p.contrast, 0.0f, 2.0f);
            ImGui::SliderFloat("Saturation", &p.saturation, 0.0f, 2.0f);
            ImGui::SliderFloat("Temperature", &p.temperature, -1.0f, 1.0f);
            ImGui::SliderFloat("Tint", &p.tint, -1.0f, 1.0f);
            ImGui::SliderFloat("Bloom threshold", &p.bloomThreshold, 0.0f, 4.0f);
            ImGui::SliderFloat("Bloom intensity", &p.bloomIntensity, 0.0f, 2.0f);
        }

        // ── Sky ───────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Sky")) {
            ImGui::ColorEdit3("Horizon (overcast)", &p.skyHorizonOvercast.x);
            ImGui::ColorEdit3("Horizon (clear)", &p.skyHorizonClear.x);
            ImGui::ColorEdit3("Zenith (overcast)", &p.skyZenithOvercast.x);
            ImGui::ColorEdit3("Zenith (clear)", &p.skyZenithClear.x);
            ImGui::SliderFloat("Clarity", &p.clarity, 0.0f, 1.0f);
            ImGui::SliderFloat("Sun disc exp min", &p.sunDiscExpMin, 1.0f, 256.0f);
            ImGui::SliderFloat("Sun disc exp max", &p.sunDiscExpMax, 1.0f, 512.0f);
            ImGui::SliderFloat("Sun disc str min", &p.sunDiscStrMin, 0.0f, 1.0f);
            ImGui::SliderFloat("Sun disc str max", &p.sunDiscStrMax, 0.0f, 1.0f);
        }

        // ── Sun / light ───────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Sun / Light")) {
            ImGui::ColorEdit3("Sun color", &p.sunColor.x);
            ImGui::SliderFloat("Ambient", &p.sunAmbient, 0.0f, 1.0f);
            ImGui::SliderFloat("Azimuth", &p.sunAzimuth, -kPi, kPi);
            ImGui::SliderFloat("Elevation", &p.sunElevation, 0.0f, kPi * 0.5f);
            ImGui::Checkbox("Sun gizmo (drag to orient)", &p.showSunGizmo);
            if (p.showSunGizmo && ImGui::Button("Reset gizmo"))
                p.sunGizmoRot = glm::mat4(1.0f);
        }

        // ── Steering (pose / fix the wheel rotation) ──────────────────
        if (ImGui::CollapsingHeader("Steering")) {
            ImGui::Checkbox("Override (pose wheel)", &p.steerOverride);
            if (ImGui::SliderFloat("Angle (deg)", &p.steerAngleDeg, -p.steerMaxDeg, p.steerMaxDeg))
                p.steerOverride = true;  // moving the slider takes control of the wheel
            ImGui::SameLine();
            if (ImGui::Button("Center"))
                p.steerAngleDeg = 0.0f;
            ImGui::Checkbox("Wheel gizmo (drag to turn)", &p.showSteerGizmo);
            ImGui::TextDisabled("edit mode only; overrides the sim while on");

            ImGui::Separator();
            // Rest-orientation calibration: edit pitch/yaw/roll OR the raw quaternion
            // to reorient the whole wheel (visible at any steer angle) until it sits
            // straight. Applies in edit mode; independent of the left-right spin.
            ImGui::Checkbox("Axis correction", &p.steerAxisEdit);
            if (p.steerAxisEdit) {
                bool ch = false;
                ch |= ImGui::SliderFloat("Pitch", &p.steerEuler.x, -180.0f, 180.0f);
                ch |= ImGui::SliderFloat("Yaw", &p.steerEuler.y, -180.0f, 180.0f);
                ch |= ImGui::SliderFloat("Roll", &p.steerEuler.z, -180.0f, 180.0f);
                if (ch) {  // euler → quaternion (radians; pitch,yaw,roll)
                    glm::quat q = glm::quat(glm::radians(glm::vec3(p.steerEuler)));
                    p.steerQuat = glm::vec4(q.x, q.y, q.z, q.w);
                }
                if (ImGui::DragFloat4("Quat xyzw", &p.steerQuat.x, 0.005f, -1.0f, 1.0f)) {
                    glm::quat q = glm::normalize(glm::quat(p.steerQuat.w, p.steerQuat.x, p.steerQuat.y, p.steerQuat.z));
                    p.steerQuat = glm::vec4(q.x, q.y, q.z, q.w);
                    p.steerEuler = glm::degrees(glm::eulerAngles(q));  // sync sliders
                }
                if (ImGui::Button("Reset axis")) {
                    p.steerEuler = glm::vec3(0.0f);
                    p.steerQuat  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                }
            }
        }

        // ── Road wheels (spin + steer articulation) ────────────────────
        if (ImGui::CollapsingHeader("Road Wheels")) {
            ImGui::Checkbox("Spin##wheels", &p.wheelSpinEnabled);
            ImGui::Checkbox("Steer with input##wheels", &p.wheelSteerEnabled);
            ImGui::SliderFloat("Spin multiplier", &p.wheelSpinMul, 0.0f, 10.0f);
            ImGui::TextDisabled("defaults = shipped look (on / on / 1.0)");
        }

        // ── Physics model (stub) ──────────────────────────────────────
        if (ImGui::CollapsingHeader("Physics")) {
            draw_tunable_group(p, "car");  // Physics model: rigid body (stub); unchecked = bicycle (Tunables.def)
        }

        // ── Fog ───────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Fog")) {
            ImGui::ColorEdit3("Fog color", &p.fogColor.x);
            // Display distance in metres; store in world units (1 m = 1000 WU).
            namespace uu = util::units;
            float distM  = uu::to_m(p.fogDist63);
            if (ImGui::SliderFloat("Dist @63% (m)", &distM, 1.0f, 5000.0f, "%.0f"))
                p.fogDist63 = uu::m(distM);
            ImGui::SliderFloat("Fog max", &p.fogMax, 0.0f, 1.0f);
            // Always-on aerial haze (dry days too) — dissolves distance into the sky.
            float hazeM = uu::to_m(p.hazeDist);
            if (ImGui::SliderFloat("Haze dist (m)", &hazeM, 100.0f, 5000.0f, "%.0f"))
                p.hazeDist = uu::m(hazeM);
            ImGui::SliderFloat("Haze max", &p.hazeMax, 0.0f, 1.0f);
        }

        // ── Clearcoat / Flake (car paint BRDF) ─────────────────────────
        if (ImGui::CollapsingHeader("Clearcoat / Flake")) {
            ImGui::TextDisabled("Global multipliers over the per-material table (Materials panel)");
            ImGui::SliderFloat("Clearcoat mul", &p.clearcoatMul, 0.0f, 2.0f);
            ImGui::SliderFloat("Flake density mul", &p.flakeDensityMul, 0.0f, 2.0f);
            ImGui::TextDisabled("Set flake mul to 0 to isolate the clearcoat lobe");
            ImGui::Separator();
            ImGui::SliderFloat("Clearcoat roughness", &p.clearcoatRoughness, 0.01f, 0.3f, "%.3f");
            ImGui::SliderFloat("Flake roughness", &p.flakeRoughness, 0.02f, 0.4f, "%.3f");
            ImGui::SliderFloat("Flake cell size (WU)", &p.flakeScale, 1.0f, 60.0f, "%.1f");
            ImGui::SliderFloat("Flake jitter", &p.flakeJitter, 0.0f, 1.0f);
        }

        // ── Reflections / IBL ─────────────────────────────────────────
        if (ImGui::CollapsingHeader("Reflections / IBL")) {
            draw_tunable_group(p, "reflection");  // IBL diffuse / specular, env gloss exp (Tunables.def)
        }

        // ── SSR (screen-space reflections) ────────────────────────────
        if (ImGui::CollapsingHeader("SSR")) {
            draw_tunable_group(p, "ssr");  // controls generated from Tunables.def
        }

        // ── God Rays (screen-space light shafts) ──────────────────────
        if (ImGui::CollapsingHeader("God Rays")) {
            ImGui::Checkbox("Enabled##godrays", &p.godraysEnabled);
            ImGui::SliderFloat("Density##godrays", &p.godrayDensity, 0.0f, 1.5f);
            ImGui::SliderFloat("Decay##godrays", &p.godrayDecay, 0.80f, 1.0f);
            ImGui::SliderFloat("Weight##godrays", &p.godrayWeight, 0.0f, 2.0f);
            ImGui::SliderFloat("Intensity##godrays", &p.godrayIntensity, 0.0f, 2.0f);
        }

        // ── Depth of Field ────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Depth of Field")) {
            ImGui::Checkbox("Enabled##dof", &p.dofEnabled);
            ImGui::SliderFloat("Focus dist (WU)##dof", &p.dofFocusDist, 1000.0f, 300000.0f, "%.0f");
            ImGui::SliderFloat("Focus range (WU)##dof", &p.dofFocusRange, 10000.0f, 400000.0f, "%.0f");
            ImGui::SliderFloat("Max CoC (texels)##dof", &p.dofMaxCoC, 0.0f, 20.0f, "%.1f");
        }

        // ── SSAO ──────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("SSAO")) {
            ImGui::Checkbox("Enabled", &p.ssaoEnabled);
            ImGui::SliderFloat("Radius (WU)", &p.ssaoRadius, 100.0f, 8000.0f, "%.0f");
            ImGui::SliderFloat("Bias (WU)", &p.ssaoBias, 0.0f, 500.0f, "%.0f");
            ImGui::SliderFloat("Intensity", &p.ssaoIntensity, 0.0f, 4.0f);
        }

        // ── Shadows (CSM) ─────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Shadows")) {
            ImGui::SliderFloat("Bias", &p.shadowBias, 0.0f, 0.02f, "%.5f");
            ImGui::SliderFloat("Floor", &p.shadowFloor, 0.0f, 1.0f);
            ImGui::SliderFloat("CSM far (WU)", &p.csmShadowFar, 50000.0f, 1000000.0f, "%.0f");
            ImGui::SliderFloat("Light size (PCSS)", &p.shadowLightSize, 0.0f, 20.0f, "%.2f");
            ImGui::SliderFloat("CSM split lambda", &p.csmLambda, 0.0f, 1.0f);
            ImGui::SliderFloat("Depth bias const", &p.depthBiasConst, 0.0f, 16.0f);
            ImGui::SliderFloat("Depth bias slope", &p.depthBiasSlope, 0.0f, 8.0f);
        }

        // ── Culling (per-draw distance + frustum) ─────────────────────
        if (ImGui::CollapsingHeader("Culling")) {
            ImGui::Checkbox("Enabled##cull", &p.cullEnabled);
            ImGui::Checkbox("Frustum (G-buffer)", &p.cullFrustum);
            ImGui::SliderFloat("View dist (WU)", &p.cullMainViewDist, 200000.0f, 4300000.0f, "%.0f");
            ImGui::SliderFloat("Shadow dist (WU)", &p.cullShadowDist, 100000.0f, 2000000.0f, "%.0f");
            if (p.cullMainTotal > 0)
                ImGui::Text("main:   %d / %d drawn (%.0f%% culled)", p.cullMainSubmitted, p.cullMainTotal,
                            100.0 * (1.0 - double(p.cullMainSubmitted) / double(p.cullMainTotal)));
            if (p.cullShadowTotal > 0)
                ImGui::Text("shadow: %d / %d drawn (all cascades)", p.cullShadowSubmitted, p.cullShadowTotal);
            ImGui::TextDisabled("frustum cull = 0 visible change; pull view dist in for Layer 1");
        }

        // ── Wet / Rain ────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Wet / Rain")) {
            ImGui::SliderFloat("Rain intensity", &p.rainIntensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Streak length", &p.streakLen, 0.0f, 10000.0f, "%.0f");
            draw_tunable_group(p, "wet");  // Wet porosity / roughness, Puddles, Puddle coverage (Tunables.def)
            ImGui::Separator();
            ImGui::Checkbox("Road spray##spray", &p.sprayEnabled);
            ImGui::SliderFloat("Spray density", &p.sprayDensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Spray lifetime (s)", &p.sprayLifetime, 0.2f, 4.0f);
            ImGui::SliderFloat("Spray size (WU)", &p.spraySize, 100.0f, 3000.0f, "%.0f");
            ImGui::SliderFloat("Spray opacity", &p.sprayOpacity, 0.0f, 1.0f);
            ImGui::Checkbox("Per-wheel spray (4 emitters)##wheelspray", &p.perWheelSpray);
            ImGui::TextDisabled("off = legacy single rear-axle emitter (shipped look)");
            ImGui::Separator();
            ImGui::TextDisabled("Lamp-lens wetness (CPU model; readout in Telemetry)");
            ImGui::SliderFloat("Wet rate (1/s)##lens", &p.lensWetRate, 0.0f, 2.0f);
            ImGui::SliderFloat("Clear speed start (m/s)##lens", &p.lensClearSpeedStart, 0.0f, 40.0f, "%.1f");
            ImGui::SliderFloat("Clear speed full (m/s)##lens", &p.lensClearSpeedFull, 1.0f, 60.0f, "%.1f");
            ImGui::SliderFloat("Clear rate (1/s)##lens", &p.lensClearRate, 0.0f, 10.0f);
            ImGui::SliderFloat("Drain rate (1/s)##lens", &p.lensDrainRate, 0.0f, 1.0f, "%.3f");
            ImGui::Separator();
            ImGui::TextDisabled("Slip / skid thresholds (spray boost + skid flag)");
            ImGui::SliderFloat("Skid slip ratio##slip", &p.slipSkidRatio, 0.0f, 1.0f);
            ImGui::SliderFloat("Skid slip angle (deg)##slip", &p.slipSkidAngleDeg, 0.0f, 45.0f, "%.1f");
            ImGui::SliderFloat("Boost ratio start##slip", &p.slipRatioStart, 0.0f, 1.0f);
            ImGui::SliderFloat("Boost ratio full##slip", &p.slipRatioFull, 0.0f, 1.0f);
            ImGui::SliderFloat("Boost angle start (deg)##slip", &p.slipAngleStartDeg, 0.0f, 45.0f, "%.1f");
            ImGui::SliderFloat("Boost angle full (deg)##slip", &p.slipAngleFullDeg, 0.0f, 45.0f, "%.1f");
            ImGui::SliderFloat("Spray boost max##slip", &p.slipSprayBoostMax, 1.0f, 8.0f);
        }

        // ── Car Lights (headlights / tail / brake; tasks/todo.md "Car lights") ──
        if (ImGui::CollapsingHeader("Car Lights")) {
            draw_tunable_group(p, "car_lights");  // generated from Tunables.def
            ImGui::TextDisabled("needs a car GLB with *_Lamp nodes (SWISH_CAR=.../porsche_rigged.glb)");
        }

        // ── Gauge Needles (tach / speedo calibration + damping; tasks/todo.md "Rigged Porsche" S1c) ──
        if (ImGui::CollapsingHeader("Gauge Needles")) {
            draw_tunable_group(p, "gauges");  // generated from Tunables.def; speedo table: config/gauges.toml
            ImGui::TextDisabled("needs a car GLB with Needle_* pivots (SWISH_CAR=.../porsche_rigged.glb)");
        }

        // ── Materials (per-submesh / per-material-slot editor) ────────
        if (ImGui::CollapsingHeader("Materials")) {
            if (p.matEditSlot < 0)
                p.matEditSlot = 0;
            if (p.matEditSlot >= static_cast<int>(MAT_COUNT))
                p.matEditSlot = static_cast<int>(MAT_COUNT) - 1;
            ImGui::SliderInt("Slot", &p.matEditSlot, 0, static_cast<int>(MAT_COUNT) - 1);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", materialName(p.matEditSlot));

            MaterialOverride& o = p.matOverrides[p.matEditSlot];
            ImGui::Checkbox("Override##mat", &o.enabled);
            ImGui::SliderFloat("Metalness##mat", &o.metalness, 0.0f, 1.0f);
            ImGui::SliderFloat("Roughness mul##mat", &o.roughnessMul, 0.0f, 2.0f);
            ImGui::ColorEdit3("Color##mat", &o.color.x);
            ImGui::SliderFloat("Clearcoat##mat", &o.clearcoat, 0.0f, 1.0f);
            ImGui::SliderFloat("Flake density##mat", &o.flakeDensity, 0.0f, 1.0f);
            if (ImGui::Button("Clear this slot"))
                o = MaterialOverride{};
            ImGui::SameLine();
            if (ImGui::Button("Clear all")) {
                for (uint32_t i = 0; i < MAT_COUNT; ++i)
                    p.matOverrides[i] = MaterialOverride{};
            }
            // Count how many slots are currently overridden (quick status).
            int active = 0;
            for (uint32_t i = 0; i < MAT_COUNT; ++i)
                if (p.matOverrides[i].enabled)
                    ++active;
            ImGui::TextDisabled("%d slot(s) overridden", active);
        }

        // ── Quality ───────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Quality")) {
            ImGui::SliderFloat("SSAA scale", &p.ssaaScale, 1.0f, 2.0f);
            if (ImGui::Button("Apply SSAA"))
                p.ssaaApplyRequested = true;
            ImGui::Separator();
            ImGui::TextDisabled("TAA (alternative to SSAA — drop SSAA to 1.0 to compare)");
            ImGui::Checkbox("TAA##taa", &p.taaEnabled);
            ImGui::SliderFloat("History blend", &p.taaHistoryBlend, 0.0f, 0.98f);
            ImGui::Checkbox("Motion blur##taa", &p.motionBlurEnabled);
            ImGui::SliderFloat("Motion blur scale", &p.motionBlurScale, 0.0f, 4.0f);
            // One-click 60fps AA path: native render scale + TAA (cheap AA instead of
            // 2.25× supersampling). Re-applies the render scale immediately.
            if (ImGui::Button("Perf: native + TAA")) {
                p.ssaaScale          = 1.0f;
                p.taaEnabled         = true;
                p.ssaaApplyRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quality: 1.5x SSAA")) {
                p.ssaaScale          = 1.5f;
                p.taaEnabled         = false;
                p.ssaaApplyRequested = true;
            }
        }

        // ── Render tools (GPU profiler / capture / hot-reload / pass declarations) ──
        if (ImGui::CollapsingHeader("Render tools"))
            draw_render_tools(m_tools);

        ImGui::End();
    }

    // ── Sun-orientation gizmo (ImGuizmo) ──────────────────────────────
    // A rotate handle floated ~20 m in front of the camera (so it's always in
    // view — its world position is cosmetic; only its rotation matters). Dragging
    // it rotates sunGizmoRot, from which the Renderer derives the sun direction.
    // Only in edit mode (the cursor is free) so it doesn't fight drive-mode input.
    if (p.editMode && p.showSunGizmo) {
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);

        // Place the handle in front of the camera; carry only sunGizmoRot's rotation.
        Mat4 invView = glm::inverse(view);
        Vec3 camPos  = Vec3(invView[3]);
        Vec3 fwd     = -glm::normalize(Vec3(invView[2]));
        Mat4 display = glm::translate(Mat4(1.0f), camPos + fwd * 20000.0f) * Mat4(glm::mat3(p.sunGizmoRot));

        ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::ROTATE, ImGuizmo::WORLD, &display[0][0]);
        // Keep only the rotation (drop the cosmetic translation) for the sun dir.
        p.sunGizmoRot = Mat4(glm::mat3(display));
    }

    // ── Steering-wheel gizmo (rotate handle on the wheel's spin axis) ─
    // The handle sits at the wheel pivot (fed by App); dragging it about the
    // wheel's local Z updates the steer angle. The delta rotation is measured
    // about that (arbitrary-in-world) axis via the trace formula.
    if (p.editMode && p.showSteerGizmo) {
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);

        Mat4 m     = p.steerPivotWorld;
        Mat4 delta = Mat4(1.0f);
        ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::ROTATE_Z, ImGuizmo::LOCAL, &m[0][0], &delta[0][0]);
        if (ImGuizmo::IsUsing()) {
            glm::mat3 d      = glm::mat3(delta);
            float     trace  = d[0][0] + d[1][1] + d[2][2];
            float     angle  = std::acos(glm::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f));  // magnitude
            glm::vec3 axis   = glm::vec3(d[1][2] - d[2][1], d[2][0] - d[0][2], d[0][1] - d[1][0]);
            glm::vec3 wheelZ = glm::normalize(glm::vec3(p.steerPivotWorld[2]));
            if (glm::dot(axis, wheelZ) < 0.0f)
                angle = -angle;
            p.steerAngleDeg = glm::clamp(p.steerAngleDeg - glm::degrees(angle), -p.steerMaxDeg, p.steerMaxDeg);
            p.steerOverride = true;
        }
    }

    ImGui::Render();
}

// ══════════════════════════════════════════════════════════════════════
// record — begin our render pass and emit ImGui's draw data.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::record(VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent) {
    if (!m_init)
        return;

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = m_renderPass;
    rp.framebuffer       = m_framebuffers[imageIndex];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent;
    // loadOp = LOAD → no clear values.
    rp.clearValueCount = 0;
    rp.pClearValues    = nullptr;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
}

// ══════════════════════════════════════════════════════════════════════
// recreate — rebuild framebuffers for the new swapchain (same format).
// ══════════════════════════════════════════════════════════════════════

void DebugUI::recreate(VkFormat fmt, const std::vector<VkImageView>& views, VkExtent2D extent) {
    if (!m_init)
        return;
    (void)fmt;  // format assumed unchanged; render pass is reused as-is.
    destroyFramebuffers();
    createFramebuffers(views, extent);
}

// ══════════════════════════════════════════════════════════════════════
// cleanup — shut down backends + context, destroy owned Vulkan objects.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::cleanup() {
    if (!m_init)
        return;

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    destroyFramebuffers();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    m_init = false;
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
