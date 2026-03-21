#include "App.h"

#include "../Window/Window.h"
// #include "../../renderer/Renderer/Renderer.h"

namespace swish {

App::App() = default;

App::~App() {
  delete m_renderer;
  delete m_window;
}

int App::run() {
  // TODO: Implement the main loop as described in App.h
  // 1. m_window = new Window(); m_window->init(800, 600, "swish");
  // 2. m_renderer = new Renderer(); m_renderer->init(*m_window);
  // 3. while (!m_window->shouldClose()) { m_window->pollEvents();
  // m_renderer->drawFrame(); }
  // 4. m_renderer->cleanup();
  // 5. m_window->cleanup();
  return 0;
}

} // namespace swish
