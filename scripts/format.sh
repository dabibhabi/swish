#!/usr/bin/env bash
# clang-format the first-party C++ (src/, tests/, tools/).
#
#   scripts/format.sh                 format in place (rewrites files)
#   scripts/format.sh --check         DRY RUN: modify nothing, list files that differ,
#                                     exit 1 if any (used by `make check`)
#   scripts/format.sh [--check] PATH  restrict to files/dirs under PATH (repeatable)
#
# Never touches vendored code (include/stb_image.h), build*/ trees or FetchContent
# checkouts (_deps/). Null-delimited find + while-read, so it is safe for odd file
# names and portable to macOS (bash 3.2, BSD find) and Linux (GNU).
# Override the binary with CLANG_FORMAT=clang-format-18.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

CF="${CLANG_FORMAT:-clang-format}"
MODE=fix
ROOTS=()
for arg in "$@"; do
    case "$arg" in
        --check) MODE=check ;;
        -h|--help) sed -n '2,14p' "${BASH_SOURCE[0]}"; exit 0 ;;
        -*) echo "format.sh: unknown option $arg" >&2; exit 2 ;;
        *) ROOTS+=("$arg") ;;
    esac
done
if [ "${#ROOTS[@]}" -eq 0 ]; then ROOTS=(src tests tools); fi

if ! command -v "$CF" >/dev/null 2>&1; then
    echo "format.sh: '$CF' not found (macOS: brew install clang-format; Arch: pacman -S clang)" >&2
    exit 127
fi

list_files() {
    find "${ROOTS[@]}" \
        \( -name build -o -name 'build-*' -o -name _deps -o -name '.venv' -o -name node_modules \) -prune -o \
        -type f ! -name 'stb_*.h' \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.cc' \) -print0
}

if [ "$MODE" = fix ]; then
    n=0
    while IFS= read -r -d '' f; do
        "$CF" -i --style=file "$f"
        n=$((n + 1))
    done < <(list_files)
    echo "format.sh: formatted $n files"
    exit 0
fi

# --check: report-only
total=0
bad=0
while IFS= read -r -d '' f; do
    total=$((total + 1))
    if ! "$CF" --dry-run --Werror --style=file "$f" >/dev/null 2>&1; then
        echo "needs formatting: $f"
        bad=$((bad + 1))
    fi
done < <(list_files)
echo "format.sh --check: $bad of $total files differ from .clang-format"
[ "$bad" -eq 0 ]
