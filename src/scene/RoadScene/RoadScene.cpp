#include "RoadScene.h"

#include "utils/color/Color.h"

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// RoadScene constructors
// ══════════════════════════════════════════════════════════════════════

RoadScene::RoadScene() : RoadScene(load_road_config(CONFIG_DIR "road.bin")) {}

RoadScene::RoadScene(const RoadConfig& cfg)
    : m_road_length(cfg.road_length),
      m_lane_width(cfg.lane_width),
      m_lane_count(cfg.lane_count),
      m_shoulder_width_wb(cfg.shoulder_width_wb),
      m_shoulder_width_eb(cfg.shoulder_width_eb),
      m_grass_extent(cfg.grass_extent),
      m_barrier_width(cfg.barrier_width),
      m_barrier_height(cfg.barrier_height),
      m_rail_width(cfg.rail_width),
      m_rail_height(cfg.rail_height),
      m_curb_height(cfg.curb_height),
      m_curb_width(cfg.curb_width),
      m_asphalt_tile(cfg.asphalt_tile),
      m_grass_tile(cfg.grass_tile),
      m_concrete_tile(cfg.concrete_tile),
      m_metal_tile(cfg.metal_tile),
      m_marking_y_offset(cfg.marking_y_offset),
      m_dash_length(cfg.dash_length),
      m_dash_gap(cfg.dash_gap),
      m_shoulder_tint(util::color::rgba(cfg.shoulder_tint)),
      m_barrier_tint(util::color::rgba(cfg.barrier_tint)),
      m_rail_tint(util::color::rgba(cfg.rail_tint)),
      m_white_marking(util::color::rgba(cfg.white_marking)),
      m_yellow_marking(util::color::rgba(cfg.yellow_marking)),
      m_ground_tint(util::color::rgba(cfg.ground_tint)),
      m_grass_tint(util::color::rgba(cfg.grass_tint)),
      m_asphalt_tint(util::color::rgba(cfg.asphalt_tint)),
      m_concrete_tint(util::color::rgba(cfg.concrete_tint)),
      m_metal_tint(util::color::rgba(cfg.metal_tint)),
      m_white_tint(util::color::rgba(cfg.white_tint)),
      m_yellow_tint(util::color::rgba(cfg.yellow_tint)),
      m_black_tint(util::color::rgba(cfg.black_tint)) {}

// ══════════════════════════════════════════════════════════════════════
// Getters + Setters
// ══════════════════════════════════════════════════════════════════════

float RoadScene::get_road_length() const { return m_road_length; }
void  RoadScene::set_road_length(float length) { m_road_length = length; }

float RoadScene::get_lane_width() const { return m_lane_width; }
void  RoadScene::set_lane_width(float width) { m_lane_width = width; }

int  RoadScene::get_lane_count() const { return m_lane_count; }
void RoadScene::set_lane_count(int count) { m_lane_count = count; }

float RoadScene::get_shoulder_width_wb() const { return m_shoulder_width_wb; }
void  RoadScene::set_shoulder_width_wb(float width) { m_shoulder_width_wb = width; }

float RoadScene::get_shoulder_width_eb() const { return m_shoulder_width_eb; }
void  RoadScene::set_shoulder_width_eb(float width) { m_shoulder_width_eb = width; }

float RoadScene::get_grass_extent() const { return m_grass_extent; }
void  RoadScene::set_grass_extent(float extent) { m_grass_extent = extent; }

float RoadScene::get_barrier_width() const { return m_barrier_width; }
void  RoadScene::set_barrier_width(float width) { m_barrier_width = width; }

float RoadScene::get_barrier_height() const { return m_barrier_height; }
void  RoadScene::set_barrier_height(float height) { m_barrier_height = height; }

float RoadScene::get_rail_width() const { return m_rail_width; }
void  RoadScene::set_rail_width(float width) { m_rail_width = width; }

float RoadScene::get_rail_height() const { return m_rail_height; }
void  RoadScene::set_rail_height(float height) { m_rail_height = height; }

float RoadScene::get_asphalt_tile() const { return m_asphalt_tile; }
void  RoadScene::set_asphalt_tile(float tile) { m_asphalt_tile = tile; }

float RoadScene::get_grass_tile() const { return m_grass_tile; }
void  RoadScene::set_grass_tile(float tile) { m_grass_tile = tile; }

float RoadScene::get_concrete_tile() const { return m_concrete_tile; }
void  RoadScene::set_concrete_tile(float tile) { m_concrete_tile = tile; }

float RoadScene::get_metal_tile() const { return m_metal_tile; }
void  RoadScene::set_metal_tile(float tile) { m_metal_tile = tile; }

float RoadScene::get_marking_y_offset() const { return m_marking_y_offset; }
void  RoadScene::set_marking_y_offset(float offset) { m_marking_y_offset = offset; }

float RoadScene::get_dash_length() const { return m_dash_length; }
void  RoadScene::set_dash_length(float length) { m_dash_length = length; }

float RoadScene::get_dash_gap() const { return m_dash_gap; }
void  RoadScene::set_dash_gap(float gap) { m_dash_gap = gap; }

const Vec4& RoadScene::get_shoulder_tint() const { return m_shoulder_tint; }
void        RoadScene::set_shoulder_tint(const Vec4& tint) { m_shoulder_tint = tint; }

const Vec4& RoadScene::get_barrier_tint() const { return m_barrier_tint; }
void        RoadScene::set_barrier_tint(const Vec4& tint) { m_barrier_tint = tint; }

const Vec4& RoadScene::get_rail_tint() const { return m_rail_tint; }
void        RoadScene::set_rail_tint(const Vec4& tint) { m_rail_tint = tint; }

const Vec4& RoadScene::get_white_marking() const { return m_white_marking; }
void        RoadScene::set_white_marking(const Vec4& color) { m_white_marking = color; }

const Vec4& RoadScene::get_yellow_marking() const { return m_yellow_marking; }
void        RoadScene::set_yellow_marking(const Vec4& color) { m_yellow_marking = color; }

// ══════════════════════════════════════════════════════════════════════
// Road-element registry — the two ordered tables generate()/generate_chunk() walk
// ══════════════════════════════════════════════════════════════════════

std::span<const RoadScene::Element> RoadScene::intro_elements() {
    static const Element table[] = {
        {"grass", &layout_gen<&RoadScene::generate_grass>},
        {"road_surfaces", &layout_gen<&RoadScene::generate_road_surfaces>},
        {"shoulders", &layout_gen<&RoadScene::generate_shoulders>},
        {"jersey_barrier",
         [](const RoadScene& s, const GenCtx& c) { s.generate_jersey_barrier(c.b, c.z_near, c.z_far); }},
        {"guardrail", &layout_gen<&RoadScene::generate_guardrail>},
        {"solid_markings", &layout_gen<&RoadScene::generate_solid_markings>},
        {"dashed_markings",
         [](const RoadScene& s, const GenCtx& c) {
             s.generate_dashed_markings(c.b, c.layout, c.z_near, c.z_far, c.dash);
         }},
        {"curbs", &layout_gen<&RoadScene::generate_curbs>},
        {"rumble_strips", &layout_gen<&RoadScene::generate_rumble_strips>},
        {"dirt_strips", &layout_gen<&RoadScene::generate_dirt_strips>},
        {"ambient_occlusion", &layout_gen<&RoadScene::generate_ambient_occlusion>},
        {"hov_diamonds", &layout_gen<&RoadScene::generate_hov_diamonds>},
        {"sign_posts", &layout_gen<&RoadScene::generate_sign_posts>},
        {"overpass", &layout_gen<&RoadScene::generate_overpass>},
        {"sound_barriers", &layout_gen<&RoadScene::generate_sound_barriers>},
        {"exit_ramp", &layout_gen<&RoadScene::generate_exit_ramp>},
        {"marginal_trees", &layout_gen<&RoadScene::generate_marginal_trees>},
        {"street_lamps", [](const RoadScene& s,
                            const GenCtx& c) { s.generate_street_lamps(c.b, c.layout, c.lights, c.z_near, c.z_far); }},
    };
    return table;
}

std::span<const RoadScene::Element> RoadScene::chunk_elements() {
    static const Element table[] = {
        {"grass", &layout_gen<&RoadScene::generate_grass>},
        {"road_surfaces", &layout_gen<&RoadScene::generate_road_surfaces>},
        {"shoulders", &layout_gen<&RoadScene::generate_shoulders>},
        // Median barrier, gapped for a flat EB↔WB crossover at the chunk centre, plus the
        // crossover connector ribbon. (Demo cadence: one per 300 m chunk — the uniform tile
        // repeats it every chunk; realistic spacing needs per-location variation, later.)
        {"median_barrier_gapped",
         [](const RoadScene& s, const GenCtx& c) {
             const float xover_zc = -c.chunk_len * 0.5f;
             const float xover_gh = 45.0f * kFt;
             s.generate_jersey_barrier(c.b, c.z_near, xover_zc + xover_gh);  // near segment [xover_zc+gh .. 0]
             s.generate_jersey_barrier(c.b, xover_zc - xover_gh, c.z_far);   // far segment  [z_far .. xover_zc−gh]
             s.generate_crossover(c.b, c.layout, xover_zc, xover_gh);
         }},
        {"guardrail", &layout_gen<&RoadScene::generate_guardrail>},
        {"solid_markings", &layout_gen<&RoadScene::generate_solid_markings>},
        {"dashed_markings",
         [](const RoadScene& s, const GenCtx& c) {
             s.generate_dashed_markings(c.b, c.layout, c.z_near, c.z_far, c.dash);
         }},
        {"curbs", &layout_gen<&RoadScene::generate_curbs>},
        {"rumble_strips", &layout_gen<&RoadScene::generate_rumble_strips>},
        {"dirt_strips", &layout_gen<&RoadScene::generate_dirt_strips>},
        {"ambient_occlusion", &layout_gen<&RoadScene::generate_ambient_occlusion>},
        {"sound_barriers", &layout_gen<&RoadScene::generate_sound_barriers>},
        {"service_roads", &layout_gen<&RoadScene::generate_service_roads>},  // continuous frontage roads, both sides
        {"street_lamps", [](const RoadScene& s,
                            const GenCtx& c) { s.generate_street_lamps(c.b, c.layout, c.lights, c.z_near, c.z_far); }},
    };
    return table;
}

std::vector<std::string_view> RoadScene::intro_element_names() {
    std::vector<std::string_view> names;
    for (const Element& e : intro_elements()) names.push_back(e.name);
    return names;
}

std::vector<std::string_view> RoadScene::chunk_element_names() {
    std::vector<std::string_view> names;
    for (const Element& e : chunk_elements()) names.push_back(e.name);
    return names;
}

// ══════════════════════════════════════════════════════════════════════
// generate()
// ══════════════════════════════════════════════════════════════════════

RoadScene::SceneData RoadScene::generate() const {
    SceneData   scene;
    MeshBuilder builder(scene.meshData, scene.drawCalls);

    // Degenerate configs (no lanes or zero length) produce no geometry.
    // Without this guard, length/lane-independent features (barrier, grass)
    // would still emit vertices for an empty road.
    if (m_lane_count <= 0 || m_road_length <= 0.0f) return scene;

    float z_near = 0.0f;
    float z_far  = -m_road_length;

    RoadLayout layout{
        m_barrier_width + 3.0f * kFt,
        static_cast<float>(m_lane_count) * m_lane_width,
        -(static_cast<float>(m_lane_count) * m_lane_width),
    };

    const GenCtx ctx{builder, layout, z_near, z_far, {m_dash_length, m_dash_gap}, scene.lights, 0.0f};
    for (const Element& e : intro_elements()) e.fn(*this, ctx);

    return scene;
}

// ══════════════════════════════════════════════════════════════════════
// generate_chunk() — one tileable slice of the uniform cross-section
// ══════════════════════════════════════════════════════════════════════

RoadScene::SceneData RoadScene::generate_chunk(float chunkLen) const {
    SceneData   scene;
    MeshBuilder builder(scene.meshData, scene.drawCalls);

    if (m_lane_count <= 0 || chunkLen <= 0.0f) return scene;

    const float z_near = 0.0f;
    const float z_far  = -chunkLen;

    RoadLayout layout{
        m_barrier_width + 3.0f * kFt,
        static_cast<float>(m_lane_count) * m_lane_width,
        -(static_cast<float>(m_lane_count) * m_lane_width),
    };

    // Snap the dashed-lane cycle (dash+gap) to an exact divisor of chunkLen so the markings
    // tile with no phase jump at chunk seams (dash:gap ratio preserved). Pure: the authored
    // intro's m_dash_* are never touched.
    const road::DashPattern dash = road::snap_dash_to_chunk({m_dash_length, m_dash_gap}, chunkLen);

    // Lamp POSTS tile with the chunk; their point-lights are dropped for now
    // (per-slot light instancing across the active window is a follow-up), so
    // the emitted LightDesc list goes into a throwaway.
    std::vector<LightDesc> discardLights;

    // Continuous-span cross-section (tiles seamlessly: chunkLen is a tile-size
    // multiple, so surface UVs land on integers at the seam).
    const GenCtx ctx{builder, layout, z_near, z_far, dash, discardLights, chunkLen};
    for (const Element& e : chunk_elements()) e.fn(*this, ctx);

    return scene;
}

}  // namespace swish
