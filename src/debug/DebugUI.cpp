#include "DebugUI.h"

// The whole translation unit is empty unless the debug UI is compiled in.
#ifdef SWISH_DEBUG_UI

#include "DebugParamsIO.h"

#include "../utils/VulkanCheck.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "ImGuizmo.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

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
    initInfo.Instance        = info.instance;
    initInfo.PhysicalDevice  = info.physicalDevice;
    initInfo.Device          = info.device;
    initInfo.QueueFamily     = info.graphicsQueueFamily;
    initInfo.Queue           = info.graphicsQueue;
    initInfo.DescriptorPool  = m_descriptorPool;
    initInfo.RenderPass      = m_renderPass;
    initInfo.MinImageCount   = info.minImageCount;
    initInfo.ImageCount      = info.imageCount;
    initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
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
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;   // preserve the composited frame
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
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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
    p = DebugParams{};  // restore all defaults (keeps editMode/showPanel too)
    p.editMode  = true;
    p.showPanel = true;
}

// Flat grey overcast sky, low clarity, some fog, desaturated grade — the LIE
// (Long Island Expressway) reference look.
void presetOvercastLIE(DebugParams& p) {
    p.clarity      = 0.0f;
    p.rainIntensity = 0.0f;
    p.saturation   = 0.85f;
    p.contrast     = 1.0f;
    p.exposure     = 0.45f;
    p.fogColor     = glm::vec3(0.52f, 0.57f, 0.63f);
    p.fogDist63    = 900000.0f;
    p.fogMax       = 0.65f;
    p.sunAmbient   = 0.30f;
    p.sunColor     = glm::vec3(0.90f, 0.92f, 0.95f);  // cool, flat
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
        case MAT_ASPHALT:  return "asphalt";
        case MAT_GRASS:    return "grass";
        case MAT_CONCRETE: return "concrete";
        case MAT_METAL:    return "metal";
        case MAT_DEFAULT:  return "default";
        case MAT_RUMBLE:   return "rumble";
        case MAT_DIRT:     return "dirt";
        case MAT_TREE:     return "tree";
        default:           break;
    }
    if (i >= MAT_SIGN_0 && i <= MAT_SIGN_7)  return "sign";
    if (i >= MAT_CAR_0 && i <= MAT_CAR_19)   return "car";
    return "?";
}

void printValues(const DebugParams& p) {
    std::printf("═══ DebugParams ═══\n");
    std::printf("exposure=%.4f bloomThreshold=%.4f bloomIntensity=%.4f\n", p.exposure, p.bloomThreshold,
                p.bloomIntensity);
    std::printf("brightness=%.4f contrast=%.4f saturation=%.4f temperature=%.4f tint=%.4f\n", p.brightness, p.contrast,
                p.saturation, p.temperature, p.tint);
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
    std::printf("envGlossExp=%.3f iblDiffuse=%.3f iblSpecular=%.3f\n", p.envGlossExp, p.iblDiffuse, p.iblSpecular);
    std::printf("ssaoEnabled=%d ssaoRadius=%.1f ssaoBias=%.1f ssaoIntensity=%.3f\n", p.ssaoEnabled ? 1 : 0,
                p.ssaoRadius, p.ssaoBias, p.ssaoIntensity);
    std::printf("ssrEnabled=%d ssrIntensity=%.3f ssrMaxDist=%.1f ssrThickness=%.1f ssrStride=%.1f\n",
                p.ssrEnabled ? 1 : 0, p.ssrIntensity, p.ssrMaxDist, p.ssrThickness, p.ssrStride);
    std::printf("shadowBias=%.5f shadowFloor=%.3f csmShadowFar=%.1f csmLambda=%.3f\n", p.shadowBias, p.shadowFloor,
                p.csmShadowFar, p.csmLambda);
    std::printf("depthBiasConst=%.3f depthBiasSlope=%.3f\n", p.depthBiasConst, p.depthBiasSlope);
    std::printf("rainIntensity=%.3f wetPorosity=%.3f wetRoughness=%.3f streakLen=%.1f\n", p.rainIntensity,
                p.wetPorosity, p.wetRoughness, p.streakLen);
    std::printf("carOverride=%d carMetalness=%.3f carPaint={%.3f,%.3f,%.3f} carRoughnessMul=%.3f\n", p.carOverride ? 1 : 0,
                p.carMetalness, p.carPaint.x, p.carPaint.y, p.carPaint.z, p.carRoughnessMul);
    std::printf("ssaaScale=%.3f\n", p.ssaaScale);
    std::fflush(stdout);
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

    constexpr float kPi = 3.14159265358979323846f;

    if (p.showPanel) {
        ImGui::Begin("swish - debug");

        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::Checkbox("Edit mode (free cursor)", &p.editMode);

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
        // Persist the current look to CONFIG_DIR/presets/<name>.toml and reload
        // it later — or pick an existing preset from the combo to load it live.
        {
            static char nameBuf[64]                 = "lie";
            static std::vector<std::string> onDisk   = debugio::list_presets();
            static char status[96]                   = "";

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
            ImGui::SliderFloat("Exposure", &p.exposure, 0.0f, 2.0f);
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

        // ── Fog ───────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Fog")) {
            ImGui::ColorEdit3("Fog color", &p.fogColor.x);
            // Display distance in km; store in world units.
            float distKm = p.fogDist63 / 1000.0f;
            if (ImGui::SliderFloat("Dist @63% (km)", &distKm, 1.0f, 5000.0f, "%.0f"))
                p.fogDist63 = distKm * 1000.0f;
            ImGui::SliderFloat("Fog max", &p.fogMax, 0.0f, 1.0f);
        }

        // ── Reflections / IBL ─────────────────────────────────────────
        if (ImGui::CollapsingHeader("Reflections / IBL")) {
            ImGui::SliderFloat("IBL diffuse", &p.iblDiffuse, 0.0f, 3.0f);
            ImGui::SliderFloat("IBL specular", &p.iblSpecular, 0.0f, 3.0f);
            ImGui::SliderFloat("Env gloss exp (legacy)", &p.envGlossExp, 0.5f, 8.0f);
        }

        // ── SSR (screen-space reflections) ────────────────────────────
        if (ImGui::CollapsingHeader("SSR")) {
            ImGui::Checkbox("Enabled##ssr", &p.ssrEnabled);
            ImGui::SliderFloat("Intensity##ssr", &p.ssrIntensity, 0.0f, 2.0f);
            ImGui::SliderFloat("Max dist (WU)", &p.ssrMaxDist, 10000.0f, 500000.0f, "%.0f");
            ImGui::SliderFloat("Thickness (WU)", &p.ssrThickness, 200.0f, 20000.0f, "%.0f");
            ImGui::SliderFloat("Stride (WU)", &p.ssrStride, 200.0f, 10000.0f, "%.0f");
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
            ImGui::SliderFloat("CSM split lambda", &p.csmLambda, 0.0f, 1.0f);
            ImGui::SliderFloat("Depth bias const", &p.depthBiasConst, 0.0f, 16.0f);
            ImGui::SliderFloat("Depth bias slope", &p.depthBiasSlope, 0.0f, 8.0f);
        }

        // ── Wet / Rain ────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Wet / Rain")) {
            ImGui::SliderFloat("Rain intensity", &p.rainIntensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Wet porosity", &p.wetPorosity, 0.0f, 1.0f);
            ImGui::SliderFloat("Wet roughness", &p.wetRoughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Streak length", &p.streakLen, 0.0f, 10000.0f, "%.0f");
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
        }

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
