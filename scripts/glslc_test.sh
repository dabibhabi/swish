#!/usr/bin/env bash
# Smoke-compile shaders with glslc WITHOUT touching the source tree: every .spv goes
# to a throw-away temp dir that is removed on exit. Uses the same include paths as
# CMakeLists.txt (-I shaders/common -I shaders).
#
#   scripts/glslc_test.sh                    compile every shaders/*.{vert,frag,comp}
#   scripts/glslc_test.sh shaders/ssr.frag   compile only the given files
#   scripts/glslc_test.sh --debug-ui [...]   also define SWISH_DEBUG_UI (debug-UI variants)
#
# glslc lookup order: $GLSLC, PATH, $VULKAN_SDK/bin, ~/VulkanSDK/*/{macOS,x86_64}/bin.
# Exit status is 0 only if every shader compiled.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHADER_DIR="$REPO_ROOT/shaders"

find_glslc() {
    if [ -n "${GLSLC:-}" ] && [ -x "$GLSLC" ]; then echo "$GLSLC"; return 0; fi
    if command -v glslc >/dev/null 2>&1; then command -v glslc; return 0; fi
    if [ -n "${VULKAN_SDK:-}" ] && [ -x "$VULKAN_SDK/bin/glslc" ]; then echo "$VULKAN_SDK/bin/glslc"; return 0; fi
    local c
    for c in "$HOME"/VulkanSDK/*/macOS/bin/glslc "$HOME"/VulkanSDK/*/x86_64/bin/glslc; do
        if [ -x "$c" ]; then echo "$c"; return 0; fi
    done
    return 1
}

GLSLC_BIN="$(find_glslc)" || { echo "glslc_test.sh: glslc not found (set GLSLC=/path/to/glslc)" >&2; exit 127; }

DEFINES=()
FILES=()
for arg in "$@"; do
    case "$arg" in
        --debug-ui) DEFINES+=(-DSWISH_DEBUG_UI) ;;
        -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) if [ -e "$arg" ]; then FILES+=("$arg"); else FILES+=("$REPO_ROOT/$arg"); fi ;;
    esac
done
if [ "${#FILES[@]}" -eq 0 ]; then
    for f in "$SHADER_DIR"/*.vert "$SHADER_DIR"/*.frag "$SHADER_DIR"/*.comp; do
        [ -e "$f" ] && FILES+=("$f")
    done
fi

OUT="$(mktemp -d "${TMPDIR:-/tmp}/swish-glslc.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

ok=0
fail=0
for f in "${FILES[@]}"; do
    if "$GLSLC_BIN" ${DEFINES[@]+"${DEFINES[@]}"} -I "$SHADER_DIR/common" -I "$SHADER_DIR" \
            -o "$OUT/$(basename "$f").spv" "$f"; then
        ok=$((ok + 1))
    else
        echo "FAILED: $f" >&2
        fail=$((fail + 1))
    fi
done
echo "glslc_test.sh: $ok compiled, $fail failed ($GLSLC_BIN)"
[ "$fail" -eq 0 ]
