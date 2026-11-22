#!/usr/bin/env bash
# Soft file-size guard: flag first-party files that have grown past a readable size.
#
#   scripts/check_file_sizes.sh                warn (exit 0) about files over the limits
#   scripts/check_file_sizes.sh --strict       same, but exit 1 if any file is over
#   scripts/check_file_sizes.sh --cpp 600 --glsl 300     override the limits
#   SWISH_MAX_CPP_LINES=1000 SWISH_MAX_GLSL_LINES=500 scripts/check_file_sizes.sh
#
# Defaults: 800 lines for .cpp/.h/.hpp (src, tests, tools), 400 for
# .glsl/.frag/.vert/.comp (shaders/). Vendored code (include/), build*/ trees,
# _deps/ and venvs are never scanned. It is a nudge, not a gate: when a file is over,
# prefer adding a new file next to it rather than growing it (split by topic).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

MAX_CPP="${SWISH_MAX_CPP_LINES:-800}"
MAX_GLSL="${SWISH_MAX_GLSL_LINES:-400}"
STRICT=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --strict) STRICT=1 ;;
        --cpp)  MAX_CPP="${2:?--cpp needs a number}"; shift ;;
        --glsl) MAX_GLSL="${2:?--glsl needs a number}"; shift ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "check_file_sizes.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

scan() { # scan <limit> <root> <find name args...>
    local limit="$1" root="$2"; shift 2
    [ -d "$root" ] || return 0
    find "$root" \
        \( -name build -o -name 'build-*' -o -name _deps -o -name '.venv' -o -name node_modules \) -prune -o \
        -type f ! -name 'stb_*.h' \( "$@" \) -print0 |
    while IFS= read -r -d '' f; do
        n=$(wc -l < "$f" | tr -d ' ')
        if [ "$n" -gt "$limit" ]; then printf '%6d  %s  (limit %d)\n' "$n" "$f" "$limit"; fi
    done
}

OVER="$(
    {
        for r in src tests tools; do scan "$MAX_CPP" "$r" -name '*.cpp' -o -name '*.h' -o -name '*.hpp'; done
        scan "$MAX_GLSL" shaders -name '*.glsl' -o -name '*.frag' -o -name '*.vert' -o -name '*.comp'
    } | sort -rn
)"

if [ -z "$OVER" ]; then
    echo "check_file_sizes: OK (no file over ${MAX_CPP} lines C++ / ${MAX_GLSL} lines GLSL)"
    exit 0
fi

COUNT=$(printf '%s\n' "$OVER" | wc -l | tr -d ' ')
echo "check_file_sizes: WARNING - $COUNT file(s) over the soft limit (C++ ${MAX_CPP}, GLSL ${MAX_GLSL}):"
printf '%s\n' "$OVER"
echo "  -> prefer a new file over growing these; see docs/RECIPES.md. (Soft limit: exit 0 unless --strict.)"
[ "$STRICT" -eq 0 ] || exit 1
