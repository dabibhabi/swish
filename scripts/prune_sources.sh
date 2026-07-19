#!/usr/bin/env bash
# Prune the swish target's source list: remove any "src/....cpp" entry inside the
# add_executable(swish ...) block whose file no longer exists on disk. Run this
# after deleting a module so CMake doesn't fail configuring on a missing source.
#
# Usage:
#   scripts/prune_sources.sh          # rewrite CMakeLists.txt, dropping dead sources
#   scripts/prune_sources.sh --check  # report dead sources, change nothing (exit 1 if any)
#
# Wired into `make prune` / `make prune-check`.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE="$REPO_ROOT/CMakeLists.txt"

CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

if [ ! -f "$CMAKE" ]; then
    echo "Error: $CMAKE not found." >&2
    exit 1
fi

# Collect the source lines inside the swish block that point at a missing file.
# awk emits each dead source's repo-relative path; we test existence in bash so
# the file check uses the real filesystem (awk has no portable file-exists test).
mapfile -t candidates < <(
    awk '
        /^add_executable\(swish/ { inblock = 1; next }
        inblock && /^\)/         { inblock = 0 }
        inblock {
            line = $0
            gsub(/^[ \t]+|[ \t]+$/, "", line)   # trim
            if (line ~ /\.cpp$/) print line
        }
    ' "$CMAKE"
)

dead=()
for src in "${candidates[@]}"; do
    [ -f "$REPO_ROOT/$src" ] || dead+=("$src")
done

if [ "${#dead[@]}" -eq 0 ]; then
    echo "No dead sources in the swish target — CMakeLists.txt is clean."
    exit 0
fi

echo "Dead sources (listed in CMakeLists.txt but missing on disk):"
for src in "${dead[@]}"; do echo "  $src"; done

if [ "$CHECK_ONLY" -eq 1 ]; then
    echo
    echo "(--check) Nothing changed. Run 'make prune' to remove them."
    exit 1
fi

# Rewrite: drop exactly the dead-source lines within the block. Match on the
# trimmed path so leading indentation doesn't matter.
tmp="$(mktemp)"
printf '%s\n' "${dead[@]}" > "$tmp.dead"
awk -v deadfile="$tmp.dead" '
    BEGIN { while ((getline l < deadfile) > 0) dead[l] = 1 }
    /^add_executable\(swish/ { inblock = 1 }
    inblock && /^\)/         { inblock = 0 }
    {
        line = $0
        gsub(/^[ \t]+|[ \t]+$/, "", line)
        if (inblock && (line in dead)) next   # skip this dead source
        print
    }
' "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"
rm -f "$tmp.dead"

echo
echo "Removed ${#dead[@]} dead source(s) from CMakeLists.txt. Reconfigure with 'make build'."
