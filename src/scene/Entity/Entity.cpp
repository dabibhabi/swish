#include "Entity.h"

#include <glm/gtc/matrix_transform.hpp>

namespace swish {

Mat4 Entity::get_model_matrix() const {
    Mat4 T  = glm::translate(Mat4(1.f), m_position);
    Mat4 Ry = glm::rotate(Mat4(1.f), glm::radians(m_rotation.y), Vec3(0.f, 1.f, 0.f));
    Mat4 Rx = glm::rotate(Mat4(1.f), glm::radians(m_rotation.x), Vec3(1.f, 0.f, 0.f));
    Mat4 Rz = glm::rotate(Mat4(1.f), glm::radians(m_rotation.z), Vec3(0.f, 0.f, 1.f));
    Mat4 S  = glm::scale(Mat4(1.f), m_scale);
    return T * Ry * Rx * Rz * S;
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

}  // namespace swish
