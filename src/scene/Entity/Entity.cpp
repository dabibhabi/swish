#include "Entity.h"

#include <glm/gtc/matrix_transform.hpp>

namespace swish {

Mat4 Entity::get_model_matrix() const {
    Mat4 m = glm::translate(Mat4(1.f), m_position);
    m      = glm::rotate(m, glm::radians(m_rotation.y), Vec3(0.f, 1.f, 0.f));
    m      = glm::rotate(m, glm::radians(m_rotation.x), Vec3(1.f, 0.f, 0.f));
    m      = glm::rotate(m, glm::radians(m_rotation.z), Vec3(0.f, 0.f, 1.f));
    return glm::scale(m, m_scale);
}

std::vector<DrawCall> ModelEntity::get_draw_calls() const {
    Mat4                  model = get_model_matrix();
    std::vector<DrawCall> result;
    result.reserve(m_submeshes.size());

    for (const auto& s : m_submeshes) {
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
        dc.color       = s.color;
        dc.model       = model;
        result.push_back(dc);
    }
    return result;
}

std::vector<DrawCall> ModelEntity::get_glass_draw_calls() const {
    Mat4                  model = get_model_matrix();
    std::vector<DrawCall> result;
    result.reserve(m_glassSubmeshes.size());

    for (const auto& s : m_glassSubmeshes) {
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
        dc.color       = s.color;
        dc.model       = model;
        result.push_back(dc);
    }
    return result;
}

}  // namespace swish
