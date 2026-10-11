#!/usr/bin/env bash
# run_mac.sh — launch swish on the macOS host for low-latency streaming.
#
# Intended flow: Sunshine hosts the Mac desktop, Moonlight on the Arch box is
# the client. Run this script ON the Mac (inside the Sunshine "Desktop" stream).
#
#   ./scripts/run_mac.sh             # normal run, with Metal perf HUD on
#   ./scripts/run_mac.sh --capture   # grab a first-frame .gputrace, then open it
#
# The window is 800x600 (hardcoded in src/core/App/App.cpp). That's small on
# purpose: fewer pixels for Sunshine's VideoToolbox encoder = lower stream
# latency. To change it, edit the m_window->init(...) call and rebuild.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BIN="build/swish"
if [[ ! -x "$BIN" ]]; then
  echo "swish not built yet — configuring + building Release first…"
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(sysctl -n hw.logicalcpu)"
fi

# Metal's on-screen perf HUD (FPS / frame time / GPU). You can't trust
# client-side timing through Moonlight, so read perf from inside the window.
export MTL_HUD_ENABLED=1

# Keep MoltenVK quiet by default — stdout floods steal frame time over a stream.
export MVK_CONFIG_LOG_LEVEL=1

if [[ "${1:-}" == "--capture" ]]; then
  # GPU frame capture — see metal_debugging.md (Path A). Latency is irrelevant
  # here; this is for inspecting the Vulkan→Metal translation afterward.
  TRACE="/tmp/swish_frame.gputrace"
  export METAL_CAPTURE_ENABLED=1
  export MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=2                 # first frame only
  export MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE="$TRACE"
  rm -rf "$TRACE"                  # MoltenVK refuses to overwrite (it's a bundle)
  echo "Capturing first frame → $TRACE"
  "./$BIN"
  echo "Opening capture in Xcode…"
  open "$TRACE"
else
  exec "./$BIN"
fi
