#include "DebugUI.h"

// The whole translation unit is empty unless the debug UI is compiled in.
#ifdef SWISH_DEBUG_UI

#include "DebugParamsIO.h"
#include "Tunables.h"
#include "renderer/FrameCapture/FrameCapture.h"
#include "renderer/GpuProfiler/GpuProfiler.h"
#include "renderer/IBLManager/HdriCatalog.h"
#include "renderer/Passes/PassDeclarationValidator.h"
#include "renderer/Pipeline/Device/Device.h"
#include "renderer/ShaderHotReload/ShaderHotReload.h"
#include "scene/Entity/CarLooks.h"
#include "scene/Entity/CarPhysics.h"  // WetGripParams — hydroplaning thresholds for the Telemetry panel
#include "scene/LightingLab/LightingLab.h"
#include "scene/TimeOfDay/TimeOfDay.h"
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Detached window — its controls have their own GLFW surface/swapchain.
// ══════════════════════════════════════════════════════════════════════

namespace {
constexpr uint32_t kUiFramesInFlight = 2;

// UI1 (owner, 2026-11-20: "the imgui for config is really thin and hard to see"). A real TTF at a readable size,
// rasterised at the monitor's content scale (crisp on Retina) and shown at 1/scale, plus a higher-contrast dark style
// with roomier padding and visible frame borders.
constexpr float kUiFontPx = 17.0f;

void apply_readable_style(GLFWwindow* window) {
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(window, &xs, &ys);
    const float scale = std::max(1.0f, std::max(xs, ys));

    ImGuiIO& io   = ImGui::GetIO();
    ImFont*  font = nullptr;
#ifdef SWISH_IMGUI_FONT_DIR
    font = io.Fonts->AddFontFromFileTTF(SWISH_IMGUI_FONT_DIR "Roboto-Medium.ttf", kUiFontPx * scale);
#endif
    if (font) {
        io.FontGlobalScale = 1.0f / scale;  // rasterised at device pixels, laid out in points
    } else {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * 1.35f;  // fallback: the built-in bitmap font, enlarged
        io.Fonts->AddFontDefault(&cfg);
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& st      = ImGui::GetStyle();
    st.WindowPadding    = ImVec2(12.0f, 10.0f);
    st.FramePadding     = ImVec2(8.0f, 5.0f);
    st.ItemSpacing      = ImVec2(8.0f, 7.0f);
    st.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    st.IndentSpacing    = 18.0f;
    st.ScrollbarSize    = 16.0f;
    st.GrabMinSize      = 14.0f;
    st.FrameBorderSize  = 1.0f;
    st.WindowBorderSize = 0.0f;
    st.FrameRounding    = 3.0f;
    st.GrabRounding     = 3.0f;
    st.WindowRounding   = 0.0f;

    ImVec4* c                    = st.Colors;
    c[ImGuiCol_Text]             = ImVec4(0.96f, 0.97f, 0.98f, 1.00f);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.66f, 0.69f, 0.74f, 1.00f);  // hints were near-invisible grey
    c[ImGuiCol_WindowBg]         = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_Border]           = ImVec4(0.38f, 0.42f, 0.50f, 0.70f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.26f, 0.30f, 0.38f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.36f, 0.46f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.40f, 0.68f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.60f, 0.80f, 1.00f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.50f, 0.80f, 1.00f, 1.00f);
    c[ImGuiCol_Header]           = ImVec4(0.22f, 0.36f, 0.56f, 0.90f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.28f, 0.46f, 0.70f, 1.00f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.32f, 0.52f, 0.80f, 1.00f);
    c[ImGuiCol_Separator]        = ImVec4(0.38f, 0.42f, 0.50f, 0.80f);
    c[ImGuiCol_ScrollbarGrab]    = ImVec4(0.40f, 0.44f, 0.52f, 1.00f);

    // The panel's swapchain is B8G8R8A8_SRGB, so the hardware encodes on write. ImGui colours are authored in sRGB,
    // which got encoded twice (a washed, low-contrast grey panel). Store the style in linear so it shows as authored.
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        c[i].x = std::pow(c[i].x, 2.2f);
        c[i].y = std::pow(c[i].y, 2.2f);
        c[i].z = std::pow(c[i].z, 2.2f);
    }
}
}  // namespace

void DebugUI::init(const DebugUIInitInfo& info) {
    if (m_init) return;

    m_deviceOwner         = info.deviceOwner;
    m_instance            = info.instance;
    m_physicalDevice      = info.physicalDevice;
    m_device              = info.device;
    m_graphicsQueue       = info.graphicsQueue;
    m_presentQueue        = info.presentQueue;
    m_graphicsQueueFamily = info.graphicsQueueFamily;
    m_presentQueueFamily  = info.presentQueueFamily;
    m_keyEvent            = info.keyEvent;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    // UI1 (2026-11-20): wider and taller so the panel reads without squinting; it stays resizable.
    m_window = glfwCreateWindow(560, 1000, "Swish Lighting Controls", nullptr, nullptr);
    if (!m_window) throw std::runtime_error("failed to create the detached Swish controls window");
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebuffer_resize_callback);
    glfwSetKeyCallback(m_window, key_callback);
    glfwSetWindowFocusCallback(m_window, focus_callback);

    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("failed to create the detached controls Vulkan surface");
    VkBool32 supported = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_presentQueueFamily, m_surface, &supported));
    if (supported != VK_TRUE)
        throw std::runtime_error("the Vulkan present queue cannot present to the controls window");

    const auto indices = m_deviceOwner->findQueueFamilies(m_physicalDevice, m_surface);
    if (!indices.graphicsFamily || !indices.presentFamily || *indices.graphicsFamily != m_graphicsQueueFamily ||
        *indices.presentFamily != m_presentQueueFamily)
        throw std::runtime_error(
            "the controls window requires queue families that were not enabled on the Vulkan device");

    m_swapchain = std::make_unique<Swapchain>();
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    m_swapchain->init(*m_deviceOwner, m_surface, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    create_render_pass(m_swapchain->getImageFormat());
    create_framebuffers();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));

    m_commandBuffers.resize(kUiFramesInFlight);
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool        = m_commandPool;
    commandInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = kUiFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &commandInfo, m_commandBuffers.data()));

    m_imageAvailable.resize(kUiFramesInFlight);
    m_renderFinished.resize(kUiFramesInFlight);
    m_inFlightFences.resize(kUiFramesInFlight);
    m_imagesInFlight.assign(m_swapchain->getImageCount(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kUiFramesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailable[i]));
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinished[i]));
        VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]));
    }

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
    VkDescriptorPoolCreateInfo           descriptorInfo{};
    descriptorInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorInfo.maxSets       = 1000;
    descriptorInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorInfo.pPoolSizes    = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(m_device, &descriptorInfo, nullptr, &m_descriptorPool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    apply_readable_style(m_window);
    ImGui_ImplGlfw_InitForVulkan(m_window, true);
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance       = m_instance;
    initInfo.PhysicalDevice = m_physicalDevice;
    initInfo.Device         = m_device;
    initInfo.QueueFamily    = m_graphicsQueueFamily;
    initInfo.Queue          = m_graphicsQueue;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.RenderPass     = m_renderPass;
    initInfo.MinImageCount  = m_swapchain->getImageCount();
    initInfo.ImageCount     = m_swapchain->getImageCount();
    initInfo.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);

    m_init = true;
}

void DebugUI::create_render_pass(VkFormat fmt) {
    VkAttachmentDescription color{};
    color.format         = fmt;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription  subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;
    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dependency;
    VK_CHECK(vkCreateRenderPass(m_device, &info, nullptr, &m_renderPass));
}

void DebugUI::create_framebuffers() {
    const auto&      views  = m_swapchain->getImageViews();
    const VkExtent2D extent = m_swapchain->getExtent();
    m_framebuffers.resize(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
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
    m_imagesInFlight.assign(views.size(), VK_NULL_HANDLE);
}

void DebugUI::destroy_framebuffers() {
    for (VkFramebuffer fb : m_framebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();
}

void DebugUI::framebuffer_resize_callback(GLFWwindow* window, int, int) {
    auto* self = static_cast<DebugUI*>(glfwGetWindowUserPointer(window));
    if (self) self->m_resized = true;
}

void DebugUI::key_callback(GLFWwindow* window, int key, int, int action, int) {
    auto* self = static_cast<DebugUI*>(glfwGetWindowUserPointer(window));
    if (self && self->m_keyEvent) self->m_keyEvent(key, action);
}

void DebugUI::focus_callback(GLFWwindow* window, int focused) {
    if (focused) return;
    auto* self = static_cast<DebugUI*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_keyEvent) return;
    for (const int key : {GLFW_KEY_I, GLFW_KEY_K, GLFW_KEY_J, GLFW_KEY_L}) self->m_keyEvent(key, GLFW_RELEASE);
}

void DebugUI::recreate_swapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window, &width, &height);
    }
    VK_CHECK(vkDeviceWaitIdle(m_device));
    destroy_framebuffers();
    m_swapchain->cleanup(m_device);
    m_swapchain->init(*m_deviceOwner, m_surface, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    create_framebuffers();
    m_resized = false;
}

namespace {

void presetReset(DebugParams& p) {
    p           = DebugParams{};  // restore all defaults (keeps editMode/showPanel too)
    p.editMode  = false;
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
    if (u >= MAT_SIGN_0 && u <= MAT_SIGN_7) return "sign";
    if (u >= MAT_CAR_0 && u <= MAT_CAR_LAST) return "car";
    return "?";
}

// Human-readable label for CarEntity::get_gear()'s 0-indexed gear (Telemetry panel).
const char* gearLabel(int gear) {
    static constexpr const char* kLabels[7] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th"};
    return (gear >= 0 && gear < 7) ? kLabels[gear] : "?";
}

// Per-kind ImGui widget and printf-dump text: one overload per value-kind tag (utils/meta/Tags.h). A new kind
// adds one `widget` and one `dump` here (plus put/get in DebugParamsIO.cpp); the group loops never change.
void widget(const tunables::Tunable& t, tag::flag_t, bool& v) { ImGui::Checkbox(t.label, &v); }
void widget(const tunables::Tunable& t, tag::real_t, float& v) {
    ImGui::SliderFloat(t.label, &v, t.lo, t.hi, t.sliderFmt);
}
void dump(const tunables::Tunable&, tag::flag_t, bool v) { std::printf("%d", v ? 1 : 0); }
void dump(const tunables::Tunable& t, tag::real_t, float v) { std::printf("%.*f", t.dumpPrec, static_cast<double>(v)); }

// Registry-driven controls / dump for one Tunables.def group (declaration order).
void draw_tunable_group(DebugParams& p, std::string_view group) {
    for (const tunables::Tunable& t : tunables::all()) {
        if (group != t.group) continue;
        tunables::visit(t, p, [&](auto kind, auto& v) { widget(t, kind, v); });
    }
}

void dump_tunable_group(const DebugParams& p, std::string_view group) {
    bool first = true;
    for (const tunables::Tunable& t : tunables::all()) {
        if (group != t.group) continue;
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
    std::printf("autoExposure=%d evCompensation=%.3f aeSpeed=%.2f evMin=%.2f evMax=%.2f\n", p.autoExposure ? 1 : 0,
                p.evCompensation, p.aeSpeed, p.evMin, p.evMax);
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
    dump_tunable_group(p, "rain_splash");
    dump_tunable_group(p, "ruts");
    dump_tunable_group(p, "ripples");
    dump_tunable_group(p, "wiper");
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
        if (ImGui::Button("Capture LDR (F12)") && t.requestCapture) t.requestCapture(false);
        ImGui::SameLine();
        if (ImGui::Button("Capture HDR (Shift+F12)") && t.requestCapture) t.requestCapture(true);
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
            if (!t.capture->message.empty()) ImGui::TextWrapped("%s", t.capture->message.c_str());
            if (!t.capture->lastPath.empty()) ImGui::TextWrapped("Last LDR: %s", t.capture->lastPath.c_str());
            if (!t.capture->lastHdrPath.empty()) ImGui::TextWrapped("Last HDR: %s", t.capture->lastHdrPath.c_str());
        }
    }

    if (t.hotReload) {
        ImGui::SeparatorText("Shader hot-reload");
        bool enabled = t.hotReload->enabled();
        if (ImGui::Checkbox("Enabled##hotreload", &enabled) && t.setHotReloadEnabled) t.setHotReloadEnabled(enabled);
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
        if (t.decls) ImGui::TextDisabled("%zu passes checked", t.decls->size());
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
    if (!m_init) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGuiIO& io = ImGui::GetIO();

    using util::units::kPi;  // == the literal 3.14159265358979323846f (static_assert in Units.h)

    if (p.showPanel) {
        // UI1: the panel fills its own OS window (resize the window to resize the panel), so it is never a thin
        // floating box inside an otherwise empty window.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("swish - debug", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar);

        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::Checkbox("Pause simulation", &p.editMode);
        ImGui::TextDisabled("Game window: arrows. Controls window: I/K throttle + brake, J/L steer.");

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
                const float  kRedlineRpm = p.telemetryRedline;
                const float  rpmFrac     = glm::clamp(p.telemetryRpm / kRedlineRpm, 0.0f, 1.0f);
                const ImVec4 rpmColor    = rpmFrac > 0.9f   ? ImVec4(0.90f, 0.15f, 0.15f, 1.0f)   // red
                                           : rpmFrac > 0.7f ? ImVec4(0.95f, 0.75f, 0.10f, 1.0f)   // amber
                                                            : ImVec4(0.20f, 0.75f, 0.30f, 1.0f);  // green
                char         rpmLabel[32];
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
            if (ImGui::Button("Export CSV")) p.drvExportRequested = true;
            if (p.drvStatus[0]) ImGui::TextDisabled("%s", p.drvStatus);

            ImGui::SeparatorText("Force feedback (readout only)");
            ImGui::Text("SAT target: %+.2f N*m  (normalized %+.3f)", p.drvFfbTorqueNm, p.drvFfbNormalized);
            ImGui::ProgressBar(0.5f + 0.5f * p.drvFfbNormalized, ImVec2(-1.0f, 0.0f), "");
            ImGui::TextDisabled("No device output: the target is computed, not sent.");
        }

        // ── Preset buttons row ────────────────────────────────────────
        if (ImGui::Button("Reset")) presetReset(p);
        ImGui::SameLine();
        if (ImGui::Button("Overcast-LIE")) presetOvercastLIE(p);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) presetClear(p);
        ImGui::SameLine();
        if (ImGui::Button("Clear+Rain")) presetClearRain(p);
        ImGui::SameLine();
        if (ImGui::Button("Print values")) printValues(p);

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
            if (ImGui::Button("Refresh")) onDisk = debugio::list_presets();

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
            if (status[0]) ImGui::TextDisabled("%s", status);
        }

        // ── Time of day / procedural sky (lighting L2, docs/lighting-l2-l10.md) ──
        if (ImGui::CollapsingHeader("Time of day / Sky model", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Procedural sky (off = HDRI weather presets)", &p.skyProcedural);
            if (p.skyProcedural) {
                for (int i = 0; i < static_cast<int>(TimeOfDay::Count); ++i) {
                    if (i % 3 != 0) ImGui::SameLine();
                    if (ImGui::RadioButton(time_of_day_name(static_cast<TimeOfDay>(i)), p.timeOfDay == i))
                        p.timeOfDay = i;
                }
                ImGui::TextDisabled("T cycles. A preset fills the values below once; then they are yours to drag.");
                ImGui::SliderFloat("Sun elevation (rad)", &p.sunElevation, -kPi * 0.5f, kPi * 0.5f);
                ImGui::SliderFloat("Sun azimuth (rad)", &p.sunAzimuth, -kPi, kPi * 2.0f);
                ImGui::SliderFloat("Direct sun (lux)", &p.sunLux, 0.0f, 130000.0f, "%.0f");
                ImGui::SliderFloat("Moon (lux)", &p.moonLux, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("Turbidity (haze)", &p.turbidity, 1.5f, 10.0f);
                ImGui::SliderFloat("Cloud cover (G sets it)", &p.cloudCover, 0.0f, 1.0f);
                ImGui::Checkbox("Stars", &p.skyStars);
                ImGui::Checkbox("Lamps and car lights on", &p.lampsOn);
                ImGui::Text("Sky zenith about %.4g cd/m2 (sets the cube scale)", static_cast<double>(p.skyNits * 0.1f));
                ImGui::TextDisabled("Each change rebakes the sky cubes (a short hitch).");
            }
        }

        // ── Lighting L3 to L10 switches (docs/lighting-l2-l10.md) ─────
        if (ImGui::CollapsingHeader("Lighting L3-L10 (debug first)")) {
            ImGui::TextDisabled("Shadows (L3)");
            ImGui::Checkbox("Stable cascade fit + texel snapping", &p.shadowStableFit);
            ImGui::SliderFloat("Cascade blend band", &p.cascadeBlend, 0.0f, 0.4f);
            ImGui::SliderFloat("Sun angular radius (rad)", &p.sunAngularRadius, 0.0f, 0.05f, "%.5f");
            ImGui::Separator();
            ImGui::TextDisabled("Materials (L4)");
            ImGui::Checkbox("Multiscatter energy compensation", &p.energyCompensation);
            ImGui::SliderFloat("Paint flake density", &p.paintFlakeDensity, 0.0f, 1.0f);
            ImGui::Separator();
            ImGui::TextDisabled("Emissive (L5), cd/m2");
            ImGui::SliderFloat("Headlight lens", &p.lensNitsHead, 0.0f, 200000.0f, "%.0f");
            ImGui::SliderFloat("Tail lens", &p.lensNitsTail, 0.0f, 10000.0f, "%.0f");
            ImGui::SliderFloat("Brake lens", &p.lensNitsBrake, 0.0f, 50000.0f, "%.0f");
            ImGui::Separator();
            ImGui::TextDisabled("Lamps and beams (L6, L8)");
            ImGui::Checkbox("Lamps and car lights on", &p.lampsOn);
            ImGui::Checkbox("Corridor street lamps", &p.corridorLamps);
            ImGui::SliderFloat("Lamp output (lm)", &p.lampLumens, 2000.0f, 40000.0f, "%.0f");
            ImGui::Checkbox("Headlight low-beam cutoff", &p.headlightCutoff);
            ImGui::SliderFloat("Lane-marking retroreflection", &p.retroGain, 0.0f, 40.0f);
            ImGui::Text("Spot lights uploaded: %d of %d (limit %d)", p.spotsUploaded, p.spotsTotal,
                        static_cast<int>(MAX_SPOT_LIGHTS));
            ImGui::Separator();
            ImGui::TextDisabled("Wet (L10)");
            ImGui::Checkbox("Rain wets the car's exterior", &p.carWettable);
        }

        // ── Car showcase (docs/car-showcase.md) ───────────────────────
        if (ImGui::CollapsingHeader("Car showcase", p.labActive ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            ImGui::TextDisabled("Paint");
            if (ImGui::BeginCombo("Finish", paint_finish_name(p.paintFinish))) {
                for (int i = 0; i < static_cast<int>(PaintFinish::Count); ++i)
                    if (ImGui::Selectable(paint_finish_name(i), p.paintFinish == i)) p.paintFinish = i;
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Colour", paint_colour_name(p.paintColour))) {
                for (int i = 0; i < static_cast<int>(PaintColour::Count); ++i)
                    if (ImGui::Selectable(paint_colour_name(i), p.paintColour == i)) p.paintColour = i;
                ImGui::EndCombo();
            }
            ImGui::RadioButton("Clean", &p.carSurface, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Wet", &p.carSurface, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Dusty", &p.carSurface, 2);
            ImGui::Checkbox("Material fixes (rubber, glass, chrome, cabin)", &p.materialFixes);

            ImGui::Separator();
            ImGui::TextDisabled("Car lights");
            ImGui::RadioButton("Auto", &p.carLightsMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Off", &p.carLightsMode, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Parking", &p.carLightsMode, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Low beam", &p.carLightsMode, 3);
            ImGui::SameLine();
            ImGui::RadioButton("Brake", &p.carLightsMode, 4);

            ImGui::Separator();
            ImGui::TextDisabled("View");
            ImGui::Checkbox("Turntable (orbit the camera)", &p.turntable);
            if (p.turntable) {
                ImGui::SliderFloat("Orbit radius (m)", &p.turntableRadius, 3.0f, 14.0f);
                ImGui::SliderFloat("Orbit height (m)", &p.turntableHeight, 0.3f, 5.0f);
                ImGui::SliderFloat("Orbit speed (deg/s)", &p.turntableSpeed, -60.0f, 60.0f);
            }
            ImGui::Checkbox("Grounding shadow under area lights", &p.groundingShadow);
            ImGui::Checkbox("Bloom pyramid", &p.bloomPyramid);
            ImGui::Text("Area lights in the world: %d (nearest %d are used)", p.rectsTotal,
                        static_cast<int>(MAX_RECT_LIGHTS));
        }

        // ── Lighting lab (docs/lighting-lab.md) ───────────────────────
        // Teleports need the lab world (SWISH_LAB=1); the light sliders, views and read-outs work anywhere.
        if (ImGui::CollapsingHeader("Lighting lab", p.labActive ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            if (p.labActive) {
                ImGui::TextDisabled("Teleport to a station");
                const auto& stations = lab_stations();
                for (size_t i = 0; i < stations.size(); ++i) {
                    if (ImGui::Button(stations[i].name)) p.labTeleport = static_cast<int>(i);
                    if (i % 2 == 0 && i + 1 < stations.size()) ImGui::SameLine();
                }
            } else {
                ImGui::TextDisabled("Launch with SWISH_LAB=1 for the test world.");
            }

            ImGui::Separator();
            ImGui::TextDisabled("Time of day (sets sun, sky and the EV clamps; turns auto-exposure on)");
            const auto set_light = [&p](float sunLux, float skyNits, float evMin, float evMax) {
                p.sunLux       = sunLux;
                p.skyNits      = skyNits;
                p.evMin        = evMin;
                p.evMax        = evMax;
                p.autoExposure = true;
            };
            if (p.skyProcedural) {
                // With the procedural sky the three buttons are time-of-day presets (the section above has all six).
                if (ImGui::Button("Day")) p.timeOfDay = static_cast<int>(TimeOfDay::Noon);
                ImGui::SameLine();
                if (ImGui::Button("Dusk")) p.timeOfDay = static_cast<int>(TimeOfDay::Evening);
                ImGui::SameLine();
                if (ImGui::Button("Night")) p.timeOfDay = static_cast<int>(TimeOfDay::Midnight);
            } else {
                if (ImGui::Button("Day")) set_light(100000.0f, 100000.0f, 13.5f, 17.5f);
                ImGui::SameLine();
                if (ImGui::Button("Dusk")) set_light(400.0f, 400.0f, 2.0f, 13.0f);
                ImGui::SameLine();
                if (ImGui::Button("Night")) set_light(0.3f, 0.3f, -6.0f, 8.0f);
                ImGui::SliderFloat("Sun (lux)", &p.sunLux, 0.01f, 200000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Sky scale (nits)", &p.skyNits, 0.01f, 200000.0f, "%.2f",
                                   ImGuiSliderFlags_Logarithmic);
            }
            ImGui::Checkbox("Adaptive pre-exposure (needed for dusk / night)", &p.adaptivePreExposure);

            ImGui::Separator();
            ImGui::TextDisabled("View");
            ImGui::RadioButton("Normal", &p.labViewMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("False colour", &p.labViewMode, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Bloom mask", &p.labViewMode, 2);
            if (p.labViewMode == 1)
                ImGui::TextWrapped(
                    "Exposed value: deep blue < 0.01, blue < 0.05, green = middle grey (0.14 to 0.24), "
                    "yellow > 1 (above display white), red > 2 (clipped).");

            ImGui::Separator();
            ImGui::TextDisabled("Exposure meter");
            if (p.autoExposure) {
                ImGui::Text("Scene average   %.3g cd/m2", static_cast<double>(p.meter.nits));
                ImGui::Text("EV100 metered   %.2f", static_cast<double>(p.meter.ev));
                ImGui::Text("EV100 used      %.2f  (compensation %.2f, clamp %.1f .. %.1f)%s",
                            static_cast<double>(p.meter.evUsed), static_cast<double>(p.evCompensation),
                            static_cast<double>(p.evMin), static_cast<double>(p.evMax),
                            p.meter.clamp < 0 ? "  HELD AT MIN" : (p.meter.clamp > 0 ? "  HELD AT MAX" : ""));
            } else {
                ImGui::TextDisabled("Auto-exposure is off (Image / Grade): the manual Exposure slider is in use.");
            }
            ImGui::Text("HDR multiplier  %.4g", static_cast<double>(p.meter.exposure));
            ImGui::Text("Pre-exposure    %.3g", static_cast<double>(p.meter.preExposure));
            {
                // A few seconds of the EV actually used, so adaptation is visible as a curve.
                static std::array<float, 240> history{};
                static int                    head = 0;
                history[static_cast<size_t>(head)] = p.meter.evUsed;
                head                               = (head + 1) % static_cast<int>(history.size());
                ImGui::PlotLines("EV100 used", history.data(), static_cast<int>(history.size()), head, nullptr, -6.0f,
                                 20.0f, ImVec2(0.0f, 70.0f));
            }

            if (p.labActive) {
                ImGui::Separator();
                ImGui::TextDisabled("Light on the ground under the car");
                ImGui::Text("Sun            %.4g lux  (unshadowed, from the Elevation slider)",
                            static_cast<double>(p.labSunLuxGround));
                ImGui::Text("Sky visibility %.2f  (1 = open sky, 0 = deep under a roof)",
                            static_cast<double>(p.labProbe.skyVisibility));
                ImGui::Text("Lamps          %.3g lux from %d in range", static_cast<double>(p.labProbe.lampLux),
                            p.labProbe.lampsInRange);
                if (p.labProbe.nearestLampM >= 0.0f)
                    ImGui::Text("Nearest lamp   %.1f m", static_cast<double>(p.labProbe.nearestLampM));
            }
        }

        // ── Image / Grade ─────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Image / Grade", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto-exposure", &p.autoExposure);
            if (p.autoExposure) {
                ImGui::SliderFloat("AE compensation (stops)", &p.evCompensation, -3.0f, 3.0f);
                ImGui::SliderFloat("AE speed", &p.aeSpeed, 0.1f, 8.0f);
                ImGui::SliderFloat("EV100 min", &p.evMin, -6.0f, 20.0f);
                ImGui::SliderFloat("EV100 max", &p.evMax, -6.0f, 20.0f);
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
            int hdriIndex = std::clamp(p.environmentHdri, 0, static_cast<int>(kHdriOptions.size()) - 1);
            if (ImGui::BeginCombo("Environment HDRI", kHdriOptions[hdriIndex].label.data())) {
                for (int i = 0; i < static_cast<int>(kHdriOptions.size()); ++i) {
                    const bool selected = (i == hdriIndex);
                    if (ImGui::Selectable(kHdriOptions[i].label.data(), selected)) p.environmentHdri = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            int rainIndex = std::clamp(p.rainEnvironmentHdri, 0, static_cast<int>(kHdriOptions.size()) - 1);
            if (ImGui::BeginCombo("Rain HDRI", kHdriOptions[rainIndex].label.data())) {
                for (int i = 0; i < static_cast<int>(kHdriOptions.size()); ++i) {
                    const bool selected = (i == rainIndex);
                    if (ImGui::Selectable(kHdriOptions[i].label.data(), selected)) p.rainEnvironmentHdri = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Rain EV shift (stops)", &p.rainEvShift, 0.0f, 6.0f, "%.1f");
            ImGui::SliderFloat("Sky clears below wetness", &p.rainSkyClearWetness, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Rain sky fade-in (s)", &p.rainSkyFadeSeconds, 0.0f, 60.0f, "%.0f");
            ImGui::TextDisabled("any rain: Rain HDRI, no sun, clarity 0, EV clamp lowered");
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
            ImGui::SliderFloat("Elevation", &p.sunElevation, -kPi * 0.5f,
                               kPi * 0.5f);  // below 0 = sun under the horizon
            ImGui::Checkbox("Sun gizmo (drag to orient)", &p.showSunGizmo);
            if (p.showSunGizmo && ImGui::Button("Reset gizmo")) p.sunGizmoRot = glm::mat4(1.0f);
        }

        // ── Steering (pose / fix the wheel rotation) ──────────────────
        if (ImGui::CollapsingHeader("Steering")) {
            ImGui::Checkbox("Override (pose wheel)", &p.steerOverride);
            if (ImGui::SliderFloat("Angle (deg)", &p.steerAngleDeg, -p.steerMaxDeg, p.steerMaxDeg))
                p.steerOverride = true;  // moving the slider takes control of the wheel
            ImGui::SameLine();
            if (ImGui::Button("Center")) p.steerAngleDeg = 0.0f;
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
            draw_tunable_group(p, "wheels");  // Ackermann (Tunables.def; release value from config/car.toml)
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
            if (ImGui::SliderFloat("Dist @63% (m)", &distM, 1.0f, 5000.0f, "%.0f")) p.fogDist63 = uu::m(distM);
            ImGui::SliderFloat("Fog max", &p.fogMax, 0.0f, 1.0f);
            // Always-on aerial haze (dry days too) — dissolves distance into the sky.
            float hazeM = uu::to_m(p.hazeDist);
            if (ImGui::SliderFloat("Haze dist (m)", &hazeM, 100.0f, 5000.0f, "%.0f")) p.hazeDist = uu::m(hazeM);
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
            draw_tunable_group(p, "rain_splash");  // impact splashes: ring + droplets (Tunables.def)
            ImGui::Separator();
            draw_tunable_group(p, "ruts");  // wheel-track ruts: water film in the tyre paths (Tunables.def)
            ImGui::Separator();
            draw_tunable_group(p, "ripples");  // rain ripples on standing water (Tunables.def)
            ImGui::Separator();
            draw_tunable_group(p, "wiper");  // V cycles the mode; rates / pause / sweep / ease (Tunables.def)
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
            draw_tunable_group(p, "gauges");  // generated from Tunables.def; speedo table: config/car.toml [gauges]
            ImGui::TextDisabled("needs a car GLB with Needle_* pivots (SWISH_CAR=.../porsche_rigged.glb)");
        }

        // ── Materials (per-submesh / per-material-slot editor) ────────
        if (ImGui::CollapsingHeader("Materials")) {
            if (p.matEditSlot < 0) p.matEditSlot = 0;
            if (p.matEditSlot >= static_cast<int>(MAT_COUNT)) p.matEditSlot = static_cast<int>(MAT_COUNT) - 1;
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
            if (ImGui::Button("Clear this slot")) o = MaterialOverride{};
            ImGui::SameLine();
            if (ImGui::Button("Clear all")) {
                for (uint32_t i = 0; i < MAT_COUNT; ++i) p.matOverrides[i] = MaterialOverride{};
            }
            // Count how many slots are currently overridden (quick status).
            int active = 0;
            for (uint32_t i = 0; i < MAT_COUNT; ++i)
                if (p.matOverrides[i].enabled) ++active;
            ImGui::TextDisabled("%d slot(s) overridden", active);
        }

        // ── Quality ───────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Quality")) {
            ImGui::SliderFloat("SSAA scale", &p.ssaaScale, 1.0f, 2.0f);
            if (ImGui::Button("Apply SSAA")) p.ssaaApplyRequested = true;
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
        if (ImGui::CollapsingHeader("Render tools")) draw_render_tools(m_tools);

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
            if (glm::dot(axis, wheelZ) < 0.0f) angle = -angle;
            p.steerAngleDeg = glm::clamp(p.steerAngleDeg - glm::degrees(angle), -p.steerMaxDeg, p.steerMaxDeg);
            p.steerOverride = true;
        }
    }

    ImGui::Render();
}

// ══════════════════════════════════════════════════════════════════════
// Present the detached controls window.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::present() {
    if (!m_init) return;
    if (glfwWindowShouldClose(m_window)) {
        glfwSetWindowShouldClose(m_window, GLFW_FALSE);
        glfwHideWindow(m_window);
    }
    if (!glfwGetWindowAttrib(m_window, GLFW_VISIBLE)) return;
    if (m_resized) recreate_swapchain();

    VK_CHECK(vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX));
    uint32_t imageIndex = 0;
    VkResult acquired   = vkAcquireNextImageKHR(m_device, m_swapchain->getSwapchain(), UINT64_MAX,
                                                m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("failed to acquire an image for the controls window");
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        VK_CHECK(vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX));
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

    VK_CHECK(vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]));
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    const VkExtent2D extent = m_swapchain->getExtent();
    VkClearValue     clear{};
    clear.color = {{0.055f, 0.065f, 0.085f, 1.0f}};
    VkRenderPassBeginInfo render{};
    render.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render.renderPass        = m_renderPass;
    render.framebuffer       = m_framebuffers[imageIndex];
    render.renderArea.extent = extent;
    render.clearValueCount   = 1;
    render.pClearValues      = &clear;
    vkCmdBeginRenderPass(cmd, &render, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo         submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &m_imageAvailable[m_currentFrame];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &m_renderFinished[m_currentFrame];
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submit, m_inFlightFences[m_currentFrame]));

    VkPresentInfoKHR present{};
    present.sType                  = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount     = 1;
    present.pWaitSemaphores        = &m_renderFinished[m_currentFrame];
    const VkSwapchainKHR swapchain = m_swapchain->getSwapchain();
    present.swapchainCount         = 1;
    present.pSwapchains            = &swapchain;
    present.pImageIndices          = &imageIndex;
    VkResult result                = vkQueuePresentKHR(m_presentQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR || m_resized)
        recreate_swapchain();
    else if (result != VK_SUCCESS)
        throw std::runtime_error("failed to present the controls window");

    m_currentFrame = (m_currentFrame + 1) % kUiFramesInFlight;
}

void DebugUI::toggle_visible() {
    if (!m_init) return;
    if (glfwGetWindowAttrib(m_window, GLFW_VISIBLE))
        glfwHideWindow(m_window);
    else
        glfwShowWindow(m_window);
}

// ══════════════════════════════════════════════════════════════════════
// cleanup — shut down ImGui and release UI Vulkan objects before device teardown.
// ══════════════════════════════════════════════════════════════════════

void DebugUI::cleanup() {
    if (!m_init) return;
    VK_CHECK(vkDeviceWaitIdle(m_device));

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    destroy_framebuffers();
    if (m_renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    for (VkSemaphore semaphore : m_imageAvailable) vkDestroySemaphore(m_device, semaphore, nullptr);
    for (VkSemaphore semaphore : m_renderFinished) vkDestroySemaphore(m_device, semaphore, nullptr);
    for (VkFence fence : m_inFlightFences) vkDestroyFence(m_device, fence, nullptr);
    if (m_commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_swapchain) {
        m_swapchain->cleanup(m_device);
        m_swapchain.reset();
    }
    if (m_surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    m_init = false;
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
