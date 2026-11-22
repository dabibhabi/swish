#!/bin/bash
# Screenshot the debug build on the Arch box (RTX 3060, Hyprland) instead of the Mac.
#   scripts/arch/snap.sh <out.png> [VAR=value ...]     e.g. scripts/arch/snap.sh /tmp/a.png SWISH_SPAWN=bqe,1200,1,0 WAIT=15
# Steps: rsync this checkout to arch:~/swish-agent (no build dirs, no .git), incremental debug build there,
# run swish fullscreen on DP-1 (2560x1440, ws 99; MON=SWISHSHOT for headless), grim the game window, copy the PNG back.
# Debug-build spots (CorridorDriver::spawn_from_env, AppInit): SWISH_SPAWN="road,s_m,dir,laneOff" puts the car on road
#   (key lie/bqe/gcp/vanwyck/clearview/cip or index 0-5; N9: ramp<k>,u = junction ramp k at u m) at s metres, travelling dir +1 (rising s) / -1, laneOff lanes out
#   from the median edge (0.5 = lane 1 centre); SWISH_CAMERA=chase|hood|bumper|roof|side|helicopter|free-fly picks the view;
#   SWISH_TOD=0-5 the time of day (0 dawn, 2 noon, 3 golden hour, 4 evening / dusk, 5 midnight). The window title (fps)
#   is printed after the shot.
# Env: ARCH_HOST (default arch@archnyc), SKIP_SYNC=1 to reuse the last sync/build.
set -euo pipefail
HOST=${ARCH_HOST:-arch@archnyc}
OUT=${1:?usage: snap.sh out.png [VAR=value ...]}; shift
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [ -z "${SKIP_SYNC:-}" ]; then
  rsync -az --delete --exclude build --exclude build-debug --exclude .git --exclude archive --exclude scratch \
        --exclude shader-playground --exclude 'data/osm_backup*' "$ROOT/" "$HOST:swish-agent/"
  # first run configures; later runs are incremental
  ssh "$HOST" 'cd ~/swish-agent && { [ -f build-debug/CMakeCache.txt ] || cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSWISH_DEBUG_UI=ON >/dev/null; } && cmake --build build-debug -j16 2>&1 | grep -E "error|warning: unused" | head -20; test -x build-debug/swish'
fi
ssh "$HOST" "$* OUT=/tmp/swish_snap.png bash ~/swish-agent/scripts/arch/remote_shot.sh"
scp -q "$HOST:/tmp/swish_snap.png" "$OUT"
echo "saved $OUT"
