#include "ModelManager.h"

namespace swish {

ModelManager::ModelManager(Renderer& renderer)
    : m_renderer(&renderer) {}

ModelManager::~ModelManager() { cleanup(); }

void ModelManager::load_directory(const std::string& /*dir*/) {
    // Placeholder — will load glTF/glb models in a future phase.
}

void ModelManager::cleanup() {
    // Placeholder — will destroy model GPU resources in a future phase.
}

}  // namespace swish
