# Code snippets

## Detached ImGui controls window

See [DebugUI::present](../src/debug/DebugUI.cpp) for the independent acquire/record/submit/present loop. It owns a second GLFW surface and swapchain; the main renderer presents the game view separately so controls do not cover the car while it is being driven.

## Lighting workbench launch

`make lookdev` sets `SWISH_LOOKDEV=1` and launches the debug build on the focused studio pad; details are in [lighting-workbench.md](../docs/lighting-workbench.md).

The look-dev road half-length, driving bound, camera far plane, and end margin share compile-time constants in [AppInit.cpp](../src/core/App/AppInit.cpp). The car bound is derived as `roadHalfLength - cameraFar - endMargin`, keeping the road end beyond the rendered horizon.

## Lighting scaffold: physical light units

[Photometry.h](../src/utils/light/Photometry.h) holds the CPU side of the unit maths (EV100 to exposure, lumens to candela, inverse-square in metres, colour temperature, emissive packing) as stubs to fill in. It exists so the same formulas can be unit-tested before they are typed into `lighting.frag`; the guide is [L01-physical-units.md](../docs/tutorials/lighting/L01-physical-units.md).

## Lighting scaffold: stable shadow maps

[ShadowFit.h](../src/utils/light/ShadowFit.h) stubs the four pure rules behind shadow stability: a light up vector that survives a sun at the zenith (the near-road blackout), texel snapping, per-cascade bias and hysteresis when choosing which lamps cast shadows. Guides: [L03-sun-shadows.md](../docs/tutorials/lighting/L03-sun-shadows.md) and [L07-local-shadows.md](../docs/tutorials/lighting/L07-local-shadows.md).

## Lighting scaffold: time-of-day presets

[TimeOfDay.h](../src/scene/TimeOfDay/TimeOfDay.h) stubs the preset struct, the sun direction from azimuth and elevation, and the reader for [config/time_of_day.toml](../config/time_of_day.toml). It follows the `read_float` plus fallback pattern of [CarConfig.cpp](../src/scene/CarConfig/CarConfig.cpp) so a bad config can never break startup.

## Lighting scaffold: corridor street lamps

[CorridorLamps.h](../src/scene/Corridor/CorridorLamps.h) stubs lamp placement along a centre-line polyline. It is the port target for the straight-road loop in [RoadScene_Furniture.cpp](../src/scene/RoadScene/RoadScene_Furniture.cpp) (`generate_street_lamps`), needed because the driven corridor curves and currently has no lamps.

## Live HDRI selection

[HdriCatalog.h](../src/renderer/IBLManager/HdriCatalog.h) is the shared option list for the Sky panel and [WeatherController](../src/renderer/WeatherController/WeatherController.cpp). Keeping labels, filenames, exposure and yaw in one catalog ensures a debug selection performs one matching environment rebake and can be restored in a saved debug look preset.
