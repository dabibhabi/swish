#include "HudOverlay.h"

#ifdef SWISH_DEBUG_UI

#include "DebugParams.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "utils/units/Units.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/RenderScope.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace swish {

namespace {

// The game swapchain is linear (RGBA16F extended sRGB, or an _SRGB format), so authored sRGB colours are decoded.
ImU32 col(int r, int g, int b, float a = 1.0f) {
    auto lin = [](int c) { return std::pow(static_cast<float>(c) / 255.0f, 2.2f); };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(lin(r), lin(g), lin(b), a));
}

ImVec2 iv(Vec2 v) { return ImVec2(v.x, v.y); }

// Trim with "..." until it fits (the atlas has Latin-1 only, so no ellipsis glyph).
std::string fit(ImFont* f, float size, std::string s, float w) {
    if (f->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()).x <= w) return s;
    while (!s.empty() && f->CalcTextSizeA(size, FLT_MAX, 0.0f, (s + "...").c_str()).x > w) s.pop_back();
    return s + "...";
}

struct Ink {
    ImDrawList* dl;
    ImFont*     font;
    void        text(float size, float x, float y, ImU32 c, const std::string& s) const {
        dl->AddText(font, size, ImVec2(x, y), c, s.c_str());
    }
    float width(float size, const std::string& s) const {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()).x;
    }
};

void draw_strip(ImDrawList* dl, ImFont* font, ImFont* big, const HudFeed& h, ImVec2 disp, float k) {
    const ImU32 text = col(240, 242, 245), dim = col(165, 172, 184), rule = col(255, 255, 255, 0.14f);
    const float w = std::min(900.0f * k, disp.x - 40.0f * k), ht = 86.0f * k;
    const float x0 = 0.5f * (disp.x - w), y0 = disp.y - ht - 28.0f * k;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + ht), col(12, 14, 18, 0.62f), 14.0f * k);

    // Speed: big digits, "mph" beside them on the same baseline.
    const Ink bigInk{dl, big}, ink{dl, font};
    char      buf[16];
    std::snprintf(buf, sizeof buf, "%.0f", static_cast<double>(h.speedMph));
    const float sz = 62.0f * k, cx = x0 + 128.0f * k;
    bigInk.text(sz, cx - bigInk.width(sz, buf), y0 + 0.5f * (ht - sz), text, buf);
    ink.text(19.0f * k, cx + 6.0f * k, y0 + 0.5f * (ht - sz) + sz * 0.62f, dim, "mph");
    if (!h.corridor) return;

    // Where: district over road piece.
    float x = x0 + 200.0f * k;
    dl->AddLine(ImVec2(x, y0 + 16.0f * k), ImVec2(x, y0 + ht - 16.0f * k), rule, 1.5f * k);
    x += 22.0f * k;
    std::string district = h.district;
    for (char& c : district) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ink.text(16.0f * k, x, y0 + 18.0f * k, dim, district);
    // N9: Interstate shield (MUTCD M1-1: blue, red crown) before the road name
    float sw = 0.0f;
    if (!h.shield.empty()) {
        sw = ink.width(18.0f * k, h.shield) + 14.0f * k;
        dl->AddRectFilled(ImVec2(x, y0 + 42.0f * k), ImVec2(x + sw, y0 + 70.0f * k), col(0, 59, 135), 5.0f * k);
        dl->AddRectFilled(ImVec2(x, y0 + 42.0f * k), ImVec2(x + sw, y0 + 48.0f * k), col(187, 19, 62), 5.0f * k,
                          ImDrawFlags_RoundCornersTop);
        ink.text(18.0f * k, x + 7.0f * k, y0 + 47.0f * k, text, h.shield);
        sw += 10.0f * k;
    }
    ink.text(24.0f * k, x + sw, y0 + 42.0f * k, text, h.piece);
    x += std::max(180.0f * k, sw + ink.width(24.0f * k, h.piece) + 8.0f * k);
    dl->AddLine(ImVec2(x, y0 + 16.0f * k), ImVec2(x, y0 + ht - 16.0f * k), rule, 1.5f * k);
    x += 22.0f * k;

    // Next exit: green MUTCD-style tab, shields + places, distance right-aligned.
    const float xr = x0 + w - 24.0f * k;
    if (h.tab.empty()) {
        ink.text(20.0f * k, x, y0 + 0.5f * ht - 11.0f * k, dim, "No exit ahead");
        return;
    }
    // N9: work-zone orange (MUTCD 6F.02) with black legend for a closed exit or the road's end
    const std::string& tab = h.tab;
    const float        tw  = ink.width(18.0f * k, tab) + 18.0f * k;
    dl->AddRectFilled(ImVec2(x, y0 + 17.0f * k), ImVec2(x + tw, y0 + 43.0f * k),
                      h.warn ? col(242, 130, 12, 0.95f) : col(28, 112, 74, 0.95f), 5.0f * k);
    ink.text(18.0f * k, x + 9.0f * k, y0 + 20.0f * k, h.warn ? col(10, 10, 10) : text, tab);
    std::snprintf(buf, sizeof buf, "%.1f mi", static_cast<double>(h.exitMiles));
    const float dw = ink.width(26.0f * k, buf);
    ink.text(26.0f * k, xr - dw, y0 + 0.5f * (ht - 26.0f * k), text, buf);
    const float room = xr - dw - 24.0f * k;
    // No route shield: the place names move up beside the tab.
    const bool        shield = !h.exitShield.empty();
    const std::string top    = shield ? h.exitShield : h.exitDest;
    ink.text(20.0f * k, x + tw + 12.0f * k, y0 + 19.0f * k, text, fit(font, 20.0f * k, top, room - x - tw - 12.0f * k));
    if (shield) ink.text(18.0f * k, x, y0 + 50.0f * k, dim, fit(font, 18.0f * k, h.exitDest, room - x));
}

void draw_map(ImDrawList* dl, ImFont* font, const HudFeed& h, bool local, ImVec2 disp, float k) {
    if (h.route.empty()) return;
    const float size = 220.0f * k, pad = 24.0f * k;
    const Vec2  o(disp.x - size - pad, pad);
    dl->AddRectFilled(iv(o), iv(o + Vec2(size)), col(12, 14, 18, 0.55f), 14.0f * k);

    Vec2  centre;
    float half = 0.0f;
    hud::map_frame(h, local, centre, half);
    auto px   = [&](Vec2 w) { return iv(hud::map_px(w, centre, half, o, size)); };
    auto line = [&](const std::vector<Vec2>& pts, ImU32 c, float t) {
        std::vector<ImVec2> v;
        v.reserve(pts.size());
        for (Vec2 p : pts) v.push_back(px(p));
        dl->AddPolyline(v.data(), static_cast<int>(v.size()), c, ImDrawFlags_None, t);
    };

    dl->PushClipRect(iv(o + Vec2(2.0f * k)), iv(o + Vec2(size - 2.0f * k)), true);
    // N9: every network road and ramp dimmed, the piece the car is on lit on top
    const bool onNet = h.lit >= 0;
    for (size_t i = 0; i < h.net.size(); ++i)
        line(h.net[i], col(150, 156, 168, onNet ? 0.55f : 0.4f), (i < h.netRoads ? 2.0f : 1.0f) * k);
    line(h.route, col(205, 210, 220, onNet ? 0.55f : 0.85f), 2.0f * k);
    for (const auto& sr : h.service) line(sr, col(226, 172, 78, onNet ? 0.45f : 0.9f), 2.0f * k);
    if (onNet && static_cast<size_t>(h.lit) < h.net.size())
        line(h.net[static_cast<size_t>(h.lit)], col(120, 200, 255), 3.0f * k);
    // ~100 exits crowd the whole-route view: shorter, fainter ticks there.
    const float tick = (local ? 7.0f : 3.5f) * k;
    for (size_t i = 0; i < h.exitAt.size(); ++i) {
        const ImVec2 a = px(h.exitAt[i]);
        dl->AddLine(a, ImVec2(a.x + h.exitDir[i].x * tick, a.y + h.exitDir[i].y * tick),
                    col(240, 242, 245, local ? 0.8f : 0.45f), 1.5f * k);
    }
    // Car: arrow along the heading (screen x = world x, screen y = world z).
    const Vec2 c = hud::map_px(h.car, centre, half, o, size), f = h.heading, r(-f.y, f.x);
    // Notched chevron: a plain triangle reads ambiguously at this size.
    const float  a     = 11.0f * k;
    const ImVec2 pts[] = {iv(c + f * a), iv(c - f * a * 0.7f + r * a * 0.65f), iv(c - f * a * 0.3f),
                          iv(c - f * a * 0.7f - r * a * 0.65f)};
    dl->AddTriangleFilled(pts[0], pts[1], pts[2], col(70, 160, 255));
    dl->AddTriangleFilled(pts[0], pts[2], pts[3], col(70, 160, 255));
    dl->AddPolyline(pts, 4, col(255, 255, 255), ImDrawFlags_Closed, 1.5f * k);
    dl->PopClipRect();
    dl->AddText(font, 14.0f * k, iv(o + Vec2(12.0f * k, 9.0f * k)), col(165, 172, 184),
                local   ? "2 KM"
                : onNet ? "QUEENS"
                        : "ROUTE");
}

}  // namespace

void HudOverlay::init(const HudOverlayInit& info) {
    if (m_ctx != nullptr) return;
    m_device = info.device;

    vkx::ColorPassDesc d;
    d.color                                       = {.format  = info.format,
                                                     .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                                                     .initial = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                     .final   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    const std::array<VkSubpassDependency, 1> deps = {
        vkx::subpass_dependency(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)};
    d.dependencies = deps;
    m_pass         = vkx::make_color_pass(m_device, d);

    const VkDescriptorPoolSize pool{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets       = 4;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &pool;
    VK_CHECK(vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool));

    ImGuiContext* prev = ImGui::GetCurrentContext();
    m_ctx              = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
#ifdef SWISH_IMGUI_FONT_DIR
    // Rasterised large and drawn smaller: stays crisp from a 1080p window up to Retina.
    m_font = io.Fonts->AddFontFromFileTTF(SWISH_IMGUI_FONT_DIR "Roboto-Medium.ttf", 40.0f);
    m_big  = io.Fonts->AddFontFromFileTTF(SWISH_IMGUI_FONT_DIR "Roboto-Medium.ttf", 128.0f);
#endif
    if (m_font == nullptr) m_font = io.Fonts->AddFontDefault();
    if (m_big == nullptr) m_big = m_font;

    ImGui_ImplVulkan_InitInfo vi{};
    vi.Instance       = info.instance;
    vi.PhysicalDevice = info.physicalDevice;
    vi.Device         = info.device;
    vi.QueueFamily    = info.queueFamily;
    vi.Queue          = info.queue;
    vi.DescriptorPool = m_pool;
    vi.RenderPass     = m_pass;
    vi.MinImageCount  = 2;
    vi.ImageCount     = std::max(info.imageCount, 3u);  // vertex-buffer ring must outlast the frames in flight
    vi.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&vi);
    ImGui::SetCurrentContext(prev);
}

void HudOverlay::record(VkCommandBuffer cmd, VkFramebuffer fb, VkExtent2D extent, const DebugParams& p) {
    if (m_ctx == nullptr || !p.hudShow || extent.width == 0 || extent.height == 0) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    io.DeltaTime   = 1.0f / 60.0f;  // nothing animates
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float k =
        std::max(io.DisplaySize.y / 1080.0f, 0.8f);  // authored at 1080 px tall; floor keeps small windows legible
    draw_strip(dl, m_font, m_big, p.hud, io.DisplaySize, k);
    if (p.hud.corridor) draw_map(dl, m_font, p.hud, p.hudMapLocal, io.DisplaySize, k);
    ImGui::Render();
    {
        vkx::RenderScope rs(cmd, m_pass, fb, extent, nullptr, 0);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
    ImGui::SetCurrentContext(prev);
}

void HudOverlay::cleanup() {
    if (m_ctx == nullptr) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext(m_ctx);
    ImGui::SetCurrentContext(prev == m_ctx ? nullptr : prev);
    vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    vkDestroyRenderPass(m_device, m_pass, nullptr);
    m_ctx  = nullptr;
    m_pool = VK_NULL_HANDLE;
    m_pass = VK_NULL_HANDLE;
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
