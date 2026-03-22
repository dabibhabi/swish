#pragma once

namespace swish {

class Window;
class Renderer;

// Top-level application class.
// Owns Window + Renderer. Runs the main loop.
// This is the entry point for everything.
class App {
public:
    App();
    ~App();

    // TODO: Main loop:
    //   1. Create Window and Renderer
    //   2. Init Window (GLFW)
    //   3. Init Renderer (all Vulkan setup, passing the window)
    //   4. Loop while !window.shouldClose():
    //      a. window.pollEvents()
    //      b. renderer.drawFrame()
    //   5. Cleanup renderer, then window (reverse order)
    int run();

private:
    Window*   m_window   = nullptr;
    Renderer* m_renderer = nullptr;
};

}  // namespace swish
