#include "CorridorSigns.h"

#include "CorridorEdges.h"
#include "CorridorScene.h"
#include "SignAtlas.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace swish {

using util::units::ft;
using util::units::m;
namespace rs = road::signs;
namespace sa = sign_atlas;

namespace {

const Vec3 kUp(0.0f, 1.0f, 0.0f);
const Vec4 kGalv(0.62f, 0.63f, 0.65f, 1.0f);  // galvanised, weathered (D2 steel)
const Vec4 kAlu(0.74f, 0.75f, 0.76f, 1.0f);   // mill-finish aluminium sign backs

enum class Pass { Steel, Back, Footing, Panel, Kit };

// Round pipe p to q (8 sides, open ends), any direction.
void tube(MeshBuilder& b, const Vec3& p, const Vec3& q, float r) {
    const Vec3 ax = glm::normalize(q - p);
    const Vec3 s  = glm::normalize(glm::cross(ax, std::abs(ax.y) > 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : kUp)),
               t  = glm::cross(ax, s);
    for (int k = 0; k < 8; ++k) {
        const float a0 = static_cast<float>(k) * util::units::kPi / 4.0f, a1 = a0 + util::units::kPi / 4.0f;
        const Vec3  e0 = (s * std::cos(a0) + t * std::sin(a0)) * r, e1 = (s * std::cos(a1) + t * std::sin(a1)) * r;
        cmesh::face(b, p + e0, p + e1, q + e0, q + e1, glm::normalize(e0 + e1));
    }
}

// Viewer frame at a site: travel T, viewer's right R, point at (|lat| on the site's side, y, d downstream).
struct Site {
    const CorridorAlignment&  al;
    double                    s;
    int                       side;
    double                    ox, oz;
    CorridorAlignment::Sample sm = al.sample(s);
    float                     sg = static_cast<float>(side);
    Vec3                      T  = Vec3(sm.tx, 0.0f, sm.tz) * sg;
    Vec3                      R  = Vec3(-sm.tz, 0.0f, sm.tx) * sg;
    Vec3 P(float lat, float y, float d = 0.0f) const { return cmesh::at(al, s, sg * lat, y, ox, oz) + T * d; }
};

void gantry(MeshBuilder& b, Pass pass, const CorridorAlignment& al, const std::vector<CorridorPath>& paths,
            const SignGantry& g, double ox, double oz) {
    const Site  st{al, g.s, g.side, ox, oz};
    const auto& sm    = st.sm;
    const float outer = al.outer_edge(sm);
    // median column on the NJ barrier line, or in the wide grass median off the inner shoulder
    const float latIn  = al.median_extra(g.s) > m(0.5f) ? sm.med - m(rs::kPostOut) : 0.0f;
    float       latOut = outer + m(rs::kPostOut);
    float       yRoad  = std::max(al.surface_y(g.s, st.sg * sm.med), al.surface_y(g.s, st.sg * outer));
    float       rin = 1e9f, rout = 0.0f, ry = -1e9f;
    for (const CorridorPath& p : paths)
        if (p.kind == CorridorPath::Kind::Ramp && p.side == g.side && g.s >= p.s0 && g.s <= p.s1) {
            const PathPoint pt = corridor_path_at(al, p, g.s);
            rin                = std::min(rin, std::abs(pt.lat) - p.halfWidth);
            rout               = std::max(rout, std::abs(pt.lat) + p.halfWidth);
            ry                 = std::max(ry, pt.y);
        }
    if (rout > 0.0f && rin - outer < m(rs::kPostOut + 1.0f)) {  // ramp still alongside: span it too
        latOut = rout + m(rs::kPostOut);
        yRoad  = std::max(yRoad, ry);
    }
    const float      k     = m(rs::kPanelW) / static_cast<float>(sa::kCellW);  // WU per atlas pixel
    const float      mainH = static_cast<float>(sa::kMainH) * k, stripH = static_cast<float>(sa::kStripH) * k;
    const float      tabH = static_cast<float>(sa::kTabH) * k, hw = 0.5f * m(rs::kPanelW), signH = mainH + stripH;
    const float      yB   = yRoad + ft(rs::kClearFt);
    const float      yT   = yB + 0.5f * signH;
    const float      half = 0.5f * m(rs::kTrussSide), d0 = m(rs::kSignOff), d1 = d0 + 2.0f * half, dc = d0 + half;
    const float      latC = al.paint_outer(sm) - 0.5f * al.header().laneWidth;  // over the exit (right) lane
    const float      yIn  = al.surface_y(g.s, st.sg * latIn) - m(0.3f);
    const float      yOut = std::min(corridor_verge_y(al, paths, g.side, g.s, latOut), yRoad);
    const sa::Panel& pn   = sa::kPanels[g.panel];

    switch (pass) {
        case Pass::Steel: {
            const float rc = 0.5f * m(rs::kChord), rl = 0.5f * m(rs::kLacing);
            for (const float d : {d0, d1})
                for (const float y : {yT - half, yT + half}) tube(b, st.P(latIn, y, d), st.P(latOut, y, d), rc);
            const int   n    = std::max(2, static_cast<int>(std::ceil((latOut - latIn) / m(rs::kBay))));
            const float step = (latOut - latIn) / static_cast<float>(n);
            for (int i = 0; i < n; ++i) {  // Warren lacing on all four faces
                const float l0 = latIn + step * static_cast<float>(i), l1 = l0 + step;
                const float ya = (i & 1) ? yT + half : yT - half, yb = (i & 1) ? yT - half : yT + half;
                const float da = (i & 1) ? d1 : d0, db = (i & 1) ? d0 : d1;
                for (const float d : {d0, d1}) cmesh::rod(b, st.P(l0, ya, d), st.P(l1, yb, d), rl);
                for (const float y : {yT - half, yT + half}) cmesh::rod(b, st.P(l0, y, da), st.P(l1, y, db), rl);
            }
            for (const float l : {latIn, latOut}) {  // end frames
                cmesh::rod(b, st.P(l, yT - half, d0), st.P(l, yT + half, d0), rl);
                cmesh::rod(b, st.P(l, yT - half, d1), st.P(l, yT + half, d1), rl);
                for (const float y : {yT - half, yT + half}) cmesh::rod(b, st.P(l, y, d0), st.P(l, y, d1), rl);
            }
            // twin pipe columns under each truss end, braced every 1.5 m
            const float rcol = 0.5f * m(rs::kColumn), gp = 0.5f * m(rs::kColumnGap);
            for (const auto& [l, y0] : {std::pair{latIn, yIn}, std::pair{latOut, yOut}}) {
                const float base = y0 + m(rs::kFootingH), top = yT + half + m(0.15f);
                for (const float d : {dc - gp, dc + gp}) cmesh::pole(b, st.P(l, base, d), st.P(l, top, d), rcol);
                for (float y = base + m(0.8f); y < top - m(0.5f); y += m(1.5f)) {
                    cmesh::rod(b, st.P(l, y, dc - gp), st.P(l, y, dc + gp), rl);
                    cmesh::rod(b, st.P(l, y, dc - gp), st.P(l, std::min(y + m(1.5f), top - m(0.2f)), dc + gp), rl);
                }
            }
            for (const float f : {-0.7f, 0.0f, 0.7f}) {  // vertical mounting beams, clipped to the front chords
                const float l = latC + f * hw;
                cmesh::rod(b, st.P(l, yB, m(0.10f)), st.P(l, yB + signH, m(0.10f)), m(0.04f));
                for (const float y : {yT - half, yT + half})
                    cmesh::rod(b, st.P(l, y, m(0.10f)), st.P(l, y, d0), m(0.03f));
            }
            break;
        }
        case Pass::Back: {
            const float tw = static_cast<float>(pn.tab.w) * k;
            cmesh::obox(b, st.P(latC, yB + 0.5f * signH, m(0.025f)), st.R * hw, kUp * (0.5f * signH), st.T * m(0.02f));
            cmesh::obox(b, st.P(latC + hw - 0.5f * tw, yB + signH + 0.5f * tabH, m(0.025f)), st.R * (0.5f * tw),
                        kUp * (0.5f * tabH), st.T * m(0.02f));
            for (float y = yB + m(0.5f * rs::kStiffener); y < yB + signH; y += m(rs::kStiffener))
                cmesh::obox(b, st.P(latC, y, m(0.06f)), st.R * hw, kUp * m(0.015f), st.T * m(0.015f));
            break;
        }
        case Pass::Footing: {
            const float f = 0.5f * m(rs::kFooting), gp = 0.5f * m(rs::kColumnGap);
            for (const auto& [l, y0] : {std::pair{latIn, yIn}, std::pair{latOut, yOut}})
                for (const float d : {dc - gp, dc + gp})
                    cmesh::obox(b, st.P(l, y0 + 0.5f * (m(rs::kFootingH) - m(0.5f)), d), st.R * f,
                                kUp * (0.5f * (m(rs::kFootingH) + m(0.5f))), st.T * f);
            break;
        }
        case Pass::Panel: {
            const Vec3  n  = -st.T;
            const float tw = static_cast<float>(pn.tab.w) * k;
            sign_quad(b, st.P(latC, yB + stripH + 0.5f * mainH), st.R * hw, kUp * (0.5f * mainH), n, pn.main,
                      sa::kPanelsW, sa::kPanelsH);
            sign_quad(b, st.P(latC, yB + 0.5f * stripH), st.R * hw, kUp * (0.5f * stripH), n,
                      sa::kStrips[static_cast<int>(g.label)], sa::kPanelsW, sa::kPanelsH);
            sign_quad(b, st.P(latC + hw - 0.5f * tw, yB + signH + 0.5f * tabH), st.R * (0.5f * tw), kUp * (0.5f * tabH),
                      n, pn.tab, sa::kPanelsW, sa::kPanelsH);
            break;
        }
        case Pass::Kit:
            break;
    }
}

// E5-1a gore sign on two breakaway posts (slip base at the foot, hinge plate under the sign).
void gore(MeshBuilder& b, Pass pass, const CorridorAlignment& al, const std::vector<CorridorPath>& paths,
          const SignGore& g, double ox, double oz) {
    const Site  st{al, g.s, g.side, ox, oz};
    const float lat = al.outer_edge(st.sm) + 0.5f * m(rs::kGoreGap), W = m(rs::kGoreW), hw = 0.5f * W;
    const float foot = corridor_verge_y(al, paths, g.side, g.s, lat), y0 = foot + m(rs::kGoreLift);
    switch (pass) {
        case Pass::Steel:
            for (const float o : {-0.55f * hw, 0.55f * hw}) {
                cmesh::obox(b, st.P(lat + o, 0.5f * (foot - m(0.3f) + y0 + W - m(0.1f)), m(0.12f)), st.R * m(0.05f),
                            kUp * (0.5f * (y0 + W - m(0.1f) - foot + m(0.3f))), st.T * m(0.075f));
                cmesh::obox(b, st.P(lat + o, foot + m(0.06f), m(0.12f)), st.R * m(0.13f), kUp * m(0.02f),
                            st.T * m(0.13f));  // slip base
                cmesh::obox(b, st.P(lat + o, y0 - m(0.25f), m(0.12f)), st.R * m(0.08f), kUp * m(0.012f),
                            st.T * m(0.09f));  // hinge (fuse) plate
            }
            break;
        case Pass::Back:
            cmesh::obox(b, st.P(lat, y0 + hw, m(0.02f)), st.R * hw, kUp * hw, st.T * m(0.015f));
            for (const float f : {0.25f, 0.75f})
                cmesh::obox(b, st.P(lat, y0 + f * W, m(0.05f)), st.R * hw, kUp * m(0.02f), st.T * m(0.015f));
            break;
        case Pass::Kit:
            sign_quad(b, st.P(lat, y0 + hw), st.R * hw, kUp * hw, -st.T, sa::kPanels[g.panel].gore, sa::kKitW,
                      sa::kKitH);
            break;
        default:
            break;
    }
}

// D10 enhanced reference location sign: shield + direction, mile, tenth, stacked on one U-channel post.
void marker(MeshBuilder& b, Pass pass, const CorridorAlignment& al, const std::vector<CorridorPath>& paths,
            const SignMarker& mk, double ox, double oz) {
    const Site  st{al, mk.s, mk.side, ox, oz};
    const float lat = al.outer_edge(st.sm) + m(rs::kMarkerOut), W = m(rs::kMarkerW), hw = 0.5f * W;
    const float foot = corridor_verge_y(al, paths, mk.side, mk.s, lat), y0 = foot + m(rs::kMarkerLift);
    const float hl = W * 48.0f / 96.0f, hm = W * 64.0f / 96.0f, ht = W, H = hl + hm + ht;
    switch (pass) {
        case Pass::Steel:
            cmesh::obox(b, st.P(lat, 0.5f * (foot - m(0.3f) + y0 + H - m(0.05f)), m(0.06f)), st.R * m(0.035f),
                        kUp * (0.5f * (y0 + H - m(0.05f) - foot + m(0.3f))), st.T * m(0.025f));
            break;
        case Pass::Back:
            cmesh::obox(b, st.P(lat, y0 + 0.5f * H, m(0.012f)), st.R * hw, kUp * (0.5f * H), st.T * m(0.008f));
            break;
        case Pass::Kit: {
            const Vec3 n = -st.T;
            sign_quad(b, st.P(lat, y0 + 0.5f * hl), st.R * hw, kUp * (0.5f * hl), n, sa::kMarkerLow[mk.tenth / 2],
                      sa::kKitW, sa::kKitH);
            sign_quad(b, st.P(lat, y0 + hl + 0.5f * hm), st.R * hw, kUp * (0.5f * hm), n, sa::kMarkerMid[mk.mile],
                      sa::kKitW, sa::kKitH);
            sign_quad(b, st.P(lat, y0 + hl + hm + 0.5f * ht), st.R * hw, kUp * (0.5f * ht), n,
                      sa::kMarkerTop[mk.side < 0 ? 1 : 0], sa::kKitW, sa::kKitH);
            break;
        }
        default:
            break;
    }
}

// D6 landmark board on two square posts.
void board(MeshBuilder& b, Pass pass, const CorridorAlignment& al, const SignBoard& k, double ox, double oz) {
    const Site  st{al, k.s, k.side, ox, oz};
    const float hw = 0.5f * k.w, y0 = k.foot + k.lift;
    switch (pass) {
        case Pass::Steel:
            for (const float o : {-0.3f * k.w, 0.3f * k.w})
                cmesh::obox(b, st.P(k.lat + o, 0.5f * (k.foot - m(0.3f) + y0 + k.h - m(0.1f)), m(0.1f)),
                            st.R * m(0.05f), kUp * (0.5f * (y0 + k.h - m(0.1f) - k.foot + m(0.3f))), st.T * m(0.05f));
            break;
        case Pass::Back:
            cmesh::obox(b, st.P(k.lat, y0 + 0.5f * k.h, m(0.02f)), st.R * hw, kUp * (0.5f * k.h), st.T * m(0.015f));
            break;
        case Pass::Kit:
            sign_quad(b, st.P(k.lat, y0 + 0.5f * k.h), st.R * hw, kUp * (0.5f * k.h), -st.T,
                      sa::kLandmarkBoards[k.cell], sa::kKitW, sa::kKitH);
            break;
        default:
            break;
    }
}

}  // namespace

void sign_quad(MeshBuilder& b, const Vec3& c, const Vec3& r, const Vec3& u, const Vec3& n, const sa::Rect& px, int W,
               int H) {
    const float u0 = (static_cast<float>(px.x) + 0.5f) / static_cast<float>(W);
    const float u1 = (static_cast<float>(px.x + px.w) - 0.5f) / static_cast<float>(W);
    const float v0 = (static_cast<float>(px.y) + 0.5f) / static_cast<float>(H);
    const float v1 = (static_cast<float>(px.y + px.h) - 0.5f) / static_cast<float>(H);
    const Vec4  t(glm::normalize(r), 1.0f);
    Vertex      a{c - r - u, n, {u0, v1}, t}, e{c + r - u, n, {u1, v1}, t};
    Vertex      f{c - r + u, n, {u0, v0}, t}, g{c + r + u, n, {u1, v0}, t};
    if (glm::dot(glm::cross(e.position - a.position, f.position - a.position), n) < 0.0f) {
        std::swap(a, e);
        std::swap(f, g);
    }
    b.push_quad(a, e, f, g, QuadWinding::Fan);
}

double sign_wu_per_mile(const CorridorAlignment& al) {
    return static_cast<double>(ft(rs::kOneMileFt)) * al.total_length() / al.header().realLength;
}

int sign_panel(int exitNo, bool westbound) {
    for (size_t i = 0; i < std::size(sa::kPanels); ++i)
        if (sa::kPanels[i].exitNo == exitNo && (sa::kPanels[i].westbound != 0) == westbound) return static_cast<int>(i);
    return -1;
}

std::vector<AdvanceGantry> advance_gantries(double room, double wuPerMile) {
    const double one = rs::kOneMi * wuPerMile, half = rs::kHalfMi * wuPerMile, q = rs::kQuarterMi * wuPerMile;
    if (room >= one) return {{one, SignDist::OneMile}, {half, SignDist::Half}};
    if (room >= half) return {{half, SignDist::Half}};
    if (room >= q) return {{q, SignDist::Quarter}};
    return {};
}

CorridorSigns corridor_signs(const CorridorAlignment& al, const std::vector<CorridorPath>& paths) {
    CorridorSigns out;
    if (al.exits().empty() || al.crossroads().empty()) return out;
    const double        wpm = sign_wu_per_mile(al), join = static_cast<double>(m(diamond::kJoin));
    const double        deck     = static_cast<double>(corridor_cross_half(al) + m(rs::kDeckClear));
    const double        lamp     = static_cast<double>(m(kCorridorLampSpacingM));
    const double        hide     = static_cast<double>(m(rs::kDeckHide));
    const CorridorEnds& e        = al.ends();
    const auto          off_deck = [&](double s) {
        return std::none_of(al.crossroads().begin(), al.crossroads().end(),
                            [&](const CorridorCrossroad& c) { return std::abs(s - c.s) < deck; }) &&
               !al.near_bridge(s, static_cast<double>(m(rs::kDeckClear)));  // D4
    };

    for (const int side : {+1, -1}) {
        const double sg = static_cast<double>(side);
        struct Exit {
            double u;  // gore, in travel coordinates (side * s)
            int    panel;
        };
        std::vector<Exit> ex;
        for (const CorridorPath& p : paths) {
            // exit ramp: leaves the mainline (offset grows along travel)
            if (p.kind != CorridorPath::Kind::Ramp || p.side != side || (p.lat1 > p.lat0) != (side > 0)) continue;
            const double cs = side > 0 ? p.s1 + join : p.s0 - join;
            const auto   c  = std::find_if(al.crossroads().begin(), al.crossroads().end(),
                                           [&](const CorridorCrossroad& x) { return std::abs(x.s - cs) < 1000.0; });
            if (c == al.crossroads().end()) continue;
            const int panel = sign_panel(c->exitNo, side < 0);
            if (panel < 0) continue;
            const double s =
                p.s0 + static_cast<double>(ramp_u_from_offset(p, p.halfWidth + m(rs::kGoreGap))) * (p.s1 - p.s0);
            ex.push_back({sg * s, panel});
            out.gores.push_back({s, side, panel});
        }
        std::sort(ex.begin(), ex.end(), [](const Exit& a, const Exit& b) { return a.u < b.u; });
        double lo = !al.has_ends() ? (side > 0 ? 0.0 : -al.total_length()) : side > 0 ? e.open0 : -e.taper0;
        for (const Exit& x : ex) {
            for (const AdvanceGantry& a : advance_gantries(x.u - lo, wpm)) {
                double s = sg * (x.u - a.dist);
                for (const CorridorCrossroad& c : al.crossroads()) {  // off a deck and its sight shadow: before it
                    const double u = sg * (s - c.s);
                    if (u > -deck && u < hide) s = sg * (c.s - sg * deck) > lo ? c.s - sg * deck : c.s + sg * hide;
                }
                for (const CorridorBridge& b : al.bridges()) {  // D4: same for an overpass, upstream of it
                    const double d = static_cast<double>(b.half + m(rs::kDeckClear)), u = sg * (s - b.s);
                    if (u > -d && u < (b.kind == CrossingKind::LieOver ? d : hide)) s = b.s - sg * d;
                }
                const double r = std::fmod(s + sg * m(1.0f), lamp), gap = std::min(r, lamp - r);
                if (al.median_extra(s) <= m(0.5f) && gap < m(rs::kLampClear))
                    s -= sg * 2.0 * m(rs::kLampClear);  // upstream: never back onto a deck
                if (sg * s > x.u - m(50.0f)) continue;
                out.gantries.push_back({s, side, x.panel, a.label});
            }
            lo = x.u + m(rs::kAfterGore);
        }
    }

    // Mile markers every 0.2 real mile from the Manhattan end, out of the tube, clear of ramps and decks.
    const double step = static_cast<double>(rs::kMarkerStepMi) * wpm;
    const double s0 = al.has_ends() ? e.portal + m(100.0f) : 0.0, s1 = al.has_ends() ? e.taper0 : al.total_length();
    const int    nMid = static_cast<int>(std::size(sa::kMarkerMid));
    for (int k = 0; k / 5 < nMid && static_cast<double>(k) * step < s1; ++k) {
        const double s = static_cast<double>(k) * step;
        if (s < s0 || !off_deck(s)) continue;
        const float lat = al.outer_edge(al.sample(s)) + m(rs::kMarkerOut + 1.0f);  // marker spot plus a margin
        for (const int side : {+1, -1}) {
            const bool ramp = std::any_of(paths.begin(), paths.end(), [&](const CorridorPath& p) {
                return p.kind == CorridorPath::Kind::Ramp && p.side == side && s > p.s0 && s < p.s1 &&
                       std::abs(corridor_path_at(al, p, s).lat) - p.halfWidth < lat;
            });
            const bool gore = std::any_of(out.gores.begin(), out.gores.end(), [&](const SignGore& g) {
                return g.side == side && std::abs(g.s - s) < m(30.0f);
            });
            if (!ramp && !gore) out.markers.push_back({s, side, k / 5, 2 * (k % 5)});
        }
    }

    // D6 boards on the right shoulder at the nearest spot (25 m steps) that is off a deck and clear of a ramp's inner
    // edge and of the previous board; past a ramp it would stand on the cut slope, out of sight
    namespace lm = road::landmarks;
    for (const lm::Board& L : lm::kBoards) {
        const double a = exit_s(al, L.exit);
        if (a < 0.0) continue;
        const double s0   = a - static_cast<double>(m(L.before));
        const auto   fits = [&](double q) {
            const float need = al.outer_edge(al.sample(q)) + m(lm::kBoardOut) + 0.5f * m(L.w) + m(0.5f);
            return off_deck(q) &&
                   std::none_of(out.boards.begin(), out.boards.end(),
                                [&](const SignBoard& o) { return std::abs(o.s - q) < m(60.0f); }) &&
                   std::none_of(paths.begin(), paths.end(), [&](const CorridorPath& p) {
                       return p.kind == CorridorPath::Kind::Ramp && p.side == L.side && q > p.s0 - m(5.0f) &&
                              q < p.s1 + m(5.0f) &&
                              std::abs(corridor_path_at(al, p, std::clamp(q, p.s0, p.s1)).lat) - p.halfWidth < need;
                   });
        };
        double s = -1.0;
        for (int k = 0; k <= 48 && s < 0.0; ++k)  // upstream first, then downstream
            for (const double q :
                 {s0 - static_cast<double>(L.side) * k * m(25.0f), s0 + static_cast<double>(L.side) * k * m(25.0f)})
                if (s < 0.0 && fits(q)) s = q;
        if (s < 0.0) continue;
        const float lat = al.outer_edge(al.sample(s)) + m(lm::kBoardOut);
        out.boards.push_back(
            {s, L.side, lat, corridor_verge_y(al, paths, L.side, s, lat), m(L.w), m(L.h), m(L.lift), L.cell});
    }
    return out;
}

void emit_signs(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                const std::vector<CorridorPath>& paths, const CorridorSigns& signs, double from, double to, double ox,
                double oz, const CorridorLook& look) {
    const auto in = [&](double s) { return s >= from && s < to; };
    // one batch per pass for the whole segment: signs are small, draws are not
    const auto run = [&](Pass pass, const Vec4& col, MaterialId mat) {
        const uint32_t mk = b.batch_mark();
        for (const SignGantry& g : signs.gantries)
            if (in(g.s)) gantry(b, pass, al, paths, g, ox, oz);
        for (const SignGore& g : signs.gores)
            if (in(g.s)) gore(b, pass, al, paths, g, ox, oz);
        for (const SignMarker& k : signs.markers)
            if (in(k.s)) marker(b, pass, al, paths, k, ox, oz);
        for (const SignBoard& k : signs.boards)
            if (in(k.s)) board(b, pass, al, k, ox, oz);
        if (!b.close_batch(mk, col, mat)) return;
        if (pass == Pass::Steel) draws.back().metalness = 0.3f;  // weathered zinc: the oxide patina is a dielectric
        if (pass == Pass::Panel || pass == Pass::Kit) {
            DrawCall& d    = draws.back();
            d.retro        = 1.0f;  // glass-bead sheeting: the L8.2 headlight lobe (L8.3 refines)
            d.roughnessMul = signs.sheen;
            d.metalness    = 0.0f;
        }
    };
    run(Pass::Steel, kGalv, MAT_METAL);
    run(Pass::Back, kAlu, MAT_METAL);
    run(Pass::Footing, look.concrete, MAT_CONCRETE);
    run(Pass::Panel, Vec4(1.0f), MAT_SIGN_PANEL);
    run(Pass::Kit, Vec4(1.0f), MAT_SIGN_KIT);
}

}  // namespace swish
