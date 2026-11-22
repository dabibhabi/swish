#!/bin/bash
# runs on arch: launch ~/swish-agent debug, show it fullscreen on workspace 99 of MON (default DP-1, 2560x1440),
# grab the output with grim, then give MON back its previous workspace. MON=SWISHSHOT uses a headless output instead. Env passes through (e.g. SWISH_SPAWN). WAIT=s before the shot, OUT=png path.
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:0
export HYPRLAND_INSTANCE_SIGNATURE=$(ls -t $XDG_RUNTIME_DIR/hypr | head -1)
cd ~/swish-agent
WAIT=${WAIT:-20}; OUT=${OUT:-/tmp/swish_shot.png}
MON=${MON:-DP-1}
[ "$MON" = SWISHSHOT ] && { hyprctl monitors all -j | grep -q '"SWISHSHOT"' || { hyprctl output create headless SWISHSHOT >/dev/null; sleep 1; }; }
prev=$(hyprctl monitors all -j | python3 -c "import json,sys
print(next(m['activeWorkspace']['id'] for m in json.load(sys.stdin) if m['name']=='$MON'))")
hyprctl dispatch "hl.dsp.workspace.move({ workspace = 99, monitor = \"$MON\" })" >/dev/null
./build-debug/swish "$@" > /tmp/swish_run.log 2>&1 &
pid=$!
addr() { hyprctl clients -j | python3 -c "import json,sys
print(next((c['address'] for c in json.load(sys.stdin) if c['pid']==$pid and c['title'].startswith('$1')),''))"; }
for i in $(seq 1 40); do g=$(addr 'swish  |'); p=$(addr 'Swish Lighting'); [ -n "$g" ] && [ -n "$p" ] && break; sleep 1; done
[ -n "$p" ] && hyprctl dispatch "hl.dsp.window.move({ workspace = 98, window = \"address:$p\" })" >/dev/null
hyprctl dispatch "hl.dsp.window.move({ workspace = 99, window = \"address:$g\" })" >/dev/null
hyprctl dispatch "hl.dsp.focus({ workspace = 99 })" >/dev/null
hyprctl dispatch "hl.dsp.window.fullscreen({ window = \"address:$g\" })" >/dev/null
sleep 1
hyprctl dispatch "hl.dsp.focus({ window = \"address:$g\" })" >/dev/null 2>&1
# focus warps the cursor to the window centre: park it on another output (else MON's far corner) so grim misses it
read -r cx cy < <(hyprctl monitors all -j | python3 -c "import json,sys
ms=json.load(sys.stdin); m=([o for o in ms if o['name']!='$MON'] or ms)[0]
print(m['x']+m['width']-1, m['y']+m['height']-1)")
hyprctl dispatch "hl.dsp.cursor.move({ x = $cx, y = $cy })" >/dev/null
sleep "$WAIT"
grim -o "$MON" "$OUT" && echo "shot $OUT ($MON)"
hyprctl clients -j | python3 -c "import json,sys
print('title:', next((c['title'] for c in json.load(sys.stdin) if c['pid']==$pid and c['title'].startswith('swish')),''))"  # fps
kill $pid; sleep 1; kill -9 $pid 2>/dev/null
hyprctl dispatch "hl.dsp.focus({ workspace = $prev })" >/dev/null
grep -iE "error|fail|fatal" /tmp/swish_run.log | head -5
