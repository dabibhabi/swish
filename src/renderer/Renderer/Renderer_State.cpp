// Renderer state: scene-data setters, weather preset + IBL rebake, debug-param apply,
// auto-exposure update, and the thin scene-geometry delegations. Split out of Renderer.cpp
// as a pure move.
#include "Renderer.h"
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/IBLManager/IBLManager.h"
#include "renderer/MaterialDescriptors/MaterialDescriptors.h"
#include "renderer/Pipeline/Device/Device.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/SpraySystem/SpraySystem.h"
#include "renderer/TextureManager/TextureManager.h"
#include "utils/types/Types.h"

#include <glm/glm.hpp>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Scene lights + material descriptors — thin delegations to subsystems.
// ══════════════════════════════════════════════════════════════════════

void Renderer::set_scene_lights(const std::vector<LightDesc>& lights) {
    m_cameraUniforms->set_lights(lights);
}

void Renderer::set_car_spot_lights(const std::vector<SpotLightDesc>& spots) {
    m_cameraUniforms->set_spot_lights(spots);
}

void Renderer::set_rain_intensity(float intensity) {
    m_rainIntensity = intensity;
}

float Renderer::get_wetness() const {
    return m_frameState.wetness;
}

void Renderer::set_car_velocity(Vec3 v) {
    m_carVelocity = v;
}

void Renderer::set_car_position(Vec3 position) {
    m_carPosition = position;
}

void Renderer::set_wiper(const WiperFrame& wiper) {
    m_wiper = wiper;
}

void Renderer::set_weather_preset(WeatherPreset preset) {
    m_weather.set_preset(preset);
}

#ifdef SWISH_DEBUG_UI
void Renderer::set_debug_edit_mode(bool edit) {
    m_debugParams.editMode = edit;
}

void Renderer::apply_debug_params() {
    // Weather half (sun gizmo, clarity, sun colour/ambient, IBL rebake) lives in WeatherController.
    m_weather.apply_debug(m_debugParams);
    // Rain intensity slider drives the same scalar as the R-key cycle.
    m_rainIntensity = m_debugParams.rainIntensity;
    // NOTE: ssaaApplyRequested is consumed at the top of drawFrame (recreate must
    // not run mid-frame), not here — shadow depth-bias is fed in GeometryProducer::record_shadow.
}

void Renderer::set_spray_emitters(const SprayEmitter* emitters, uint32_t count) {
    if (SpraySystem* spray = m_passes.find<SpraySystem>())
        spray->set_emitters(emitters, count);
}
#endif

void Renderer::update_glass_draw_calls(const std::vector<DrawCall>& glassDCs) {
    m_geometry.set_glass_draws(glassDCs);
}

void Renderer::update_windshield_draw_calls(const std::vector<DrawCall>& windshieldDCs) {
    m_geometry.set_windshield_draws(windshieldDCs);
}

void Renderer::rebuild_material_descriptors() {
    m_materialDescriptors->rebuild(m_device->getDevice(), *m_textureManager);
}

// ══════════════════════════════════════════════════════════════════════
// Scene geometry — thin delegations to SceneGeometry. Called by Scene
// lambdas after mesh generation / on scene unload.
// ══════════════════════════════════════════════════════════════════════

void Renderer::upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_geometry.upload_scene(services(), mesh, draws);
}

void Renderer::destroy_scene_geometry() {
    m_geometry.destroy_scene(m_device->getDevice());
    m_cameraUniforms->set_lights({});
}

void Renderer::upload_chunk_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_geometry.upload_chunk(services(), mesh, draws);
}

void Renderer::set_road_chunks(float introOffsetZ, const std::vector<float>& slotOffsetsZ) {
    m_geometry.set_road_chunks(introOffsetZ, slotOffsetsZ);
}

void Renderer::upload_interchange_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_geometry.upload_interchange(services(), mesh, draws);
}

void Renderer::set_interchanges(const std::vector<float>& slotOffsetsZ) {
    m_geometry.set_interchanges(slotOffsetsZ);
}

void Renderer::add_corridor_segment(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_geometry.add_corridor_segment(services(), mesh, draws);
}

void Renderer::set_corridor_instances(const std::vector<CorridorInstance>& instances) {
    m_geometry.set_corridor_instances(instances);
}

void Renderer::upload_dynamic_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_geometry.upload_dynamic(services(), mesh, draws);
}

void Renderer::update_dynamic_draw_calls(const std::vector<DrawCall>& draws) {
    m_geometry.update_dynamic_draw_calls(draws);
}

void Renderer::destroy_dynamic_geometry() {
    m_geometry.destroy_dynamic(m_device->getDevice());
}

}  // namespace swish
