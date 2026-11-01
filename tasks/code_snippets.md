# Code snippets

## Detached ImGui controls window

See [DebugUI::present](../src/debug/DebugUI.cpp) for the independent acquire/record/submit/present loop. It owns a second GLFW surface and swapchain; the main renderer presents the game view separately so controls do not cover the car while it is being driven.

## Lighting workbench launch

`make lookdev` sets `SWISH_LOOKDEV=1` and launches the debug build on the focused studio pad; details are in [lighting-workbench.md](../docs/lighting-workbench.md).

The look-dev road half-length, driving bound, camera far plane, and end margin share compile-time constants in [AppInit.cpp](../src/core/App/AppInit.cpp). The car bound is derived as `roadHalfLength - cameraFar - endMargin`, keeping the road end beyond the rendered horizon.

## Live HDRI selection

[HdriCatalog.h](../src/renderer/IBLManager/HdriCatalog.h) is the shared option list for the Sky panel and [WeatherController](../src/renderer/WeatherController/WeatherController.cpp). Keeping labels, filenames, exposure and yaw in one catalog ensures a debug selection performs one matching environment rebake and can be restored in a saved debug look preset.
