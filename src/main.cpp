// Minimal entry point wiring up the core App.
#include "core/App/App.h"
#include "utils/io/AppPaths.h"

int main() {
    // Before any Vulkan call: a macOS .app bundle points the loader at its bundled MoltenVK driver.
    swish::util::paths::configure_process_environment();
    swish::App app;
    return app.run();
}
