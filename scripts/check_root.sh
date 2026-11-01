#!/usr/bin/env bash
# Stray-root-file guard: the repo root should hold only the entries listed in ALLOWED
# below. Anything else that is not gitignored fails the check (exit 1) - this is what
# catches accidental shell redirects (`> debug`), scratch notes and loose drafts.
#
#   scripts/check_root.sh
#
# Where things go instead:
#   notes / drafts / prompts      -> scratch/  (gitignored)
#   finished / stale docs         -> archive/docs/<same path>  (tracked; index: archive/README.md)
#   local-only bulk (refs, dumps)  -> archive/local/  (gitignored)
#   diagrams                      -> docs/diagrams/
#   big local assets              -> gitignored dirs already listed in .gitignore
# Adding a genuinely new top-level entry? Add it to ALLOWED (one line) in the same commit.
set -euo pipefail
set -f   # patterns in ALLOWED must not be glob-expanded against the root

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# One entry per line. Trailing * = prefix glob (e.g. build-*). Comments explain why.
ALLOWED='
# --- tracked project files
README.md CONTRIBUTING.md LICENSE CHANGELOG.md CLAUDE.md AGENTS.md
CMakeLists.txt CMakePresets.json Makefile
.clang-format .clangd .gitattributes .gitignore .mcp.json.example
# --- tracked directories
src shaders tests tools scripts include config assets textures docs tasks archive
.github .claude .agents .codex .obsidian
# --- git internals / generated / local-only (most are gitignored)
.git .mcp.json .DS_Store imgui.ini .cache .conductor .dist .vscode .idea
build build-* cmake-build-* scratch
# --- local-only heavy assets and reviews (see .gitignore)
blender_builder new_assets cars durango car_internals zip_files data examples assets-src
References review_*
'

is_allowed() {
    local name="$1" pat
    for pat in $(printf '%s\n' "$ALLOWED" | grep -v '^#'); do
        # shellcheck disable=SC2254
        case "$name" in $pat) return 0 ;; esac
    done
    return 1
}

bad=0
while IFS= read -r -d '' path; do
    name="${path#./}"
    [ "$name" = ".git" ] && continue
    if is_allowed "$name"; then continue; fi
    # Unexpected but gitignored = deliberately hidden; only warn.
    if git check-ignore -q -- "$name" 2>/dev/null; then
        echo "check_root: note: '$name' is not in the allowlist but is gitignored"
        continue
    fi
    echo "check_root: STRAY root entry: $name  (move it to scratch/ or archive/, or add to ALLOWED in scripts/check_root.sh)"
    bad=$((bad + 1))
done < <(find . -mindepth 1 -maxdepth 1 -print0)

if [ "$bad" -eq 0 ]; then
    echo "check_root: OK (no stray root entries)"
    exit 0
fi
exit 1
