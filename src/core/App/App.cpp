#include "App.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../Window/Window.h"

namespace swish {

App::App() = default;

App::~App() {
    delete m_renderer;
    delete m_window;
}

int App::run() {
    // Implemented the main application loop, initializing core application subsystems
    // and advancing the renderer's draw frame continuously.
    m_window = new Window();
    m_window->init(800, 600, "swish");

    m_renderer = new Renderer();
    m_renderer->init(*m_window);

    while (!m_window->shouldClose()) {
        m_window->pollEvents();
        m_renderer->drawFrame();
    }

    m_renderer->cleanup();
    m_window->cleanup();

    return 0;
}

}  // namespace swish
