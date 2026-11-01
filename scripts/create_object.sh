#!/usr/bin/env bash
# Scaffold a new swish module: creates src/<dir>/<Name>/<Name>.h and <Name>.cpp
# with a header guard, the swish namespace, and a matching include — mirroring
# the existing module layout (see src/scene/Camera/).
#
# Usage:   scripts/create_object.sh <dir-under-src> <ObjectName>
#          make new-class NAME=<ObjectName> DIR=<dir-under-src>
# Example: scripts/create_object.sh scene RoadGeometry
#            -> src/scene/RoadGeometry/RoadGeometry.h
#            -> src/scene/RoadGeometry/RoadGeometry.cpp
#
# Make executable once: chmod +x scripts/create_object.sh

set -euo pipefail

if [ "$#" -ne 2 ] || [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 <dir-under-src> <ObjectName>" >&2
    echo "Example: $0 scene RoadGeometry" >&2
    exit 1
fi

# Strip any leading "src/" and trailing slashes from the directory, and strip a
# stray .h/.cpp extension off the name — the three ways the old runs got mangled.
DIRECTORY="${1#src/}"
DIRECTORY="${DIRECTORY%/}"
OBJECT_NAME="${2%.h}"
OBJECT_NAME="${OBJECT_NAME%.cpp}"

# Guardrails: PascalCase class name (it becomes the folder, the files and the C++ type),
# and a directory that stays inside src/.
case "$OBJECT_NAME" in
    [[:upper:]]*) ;;
    *) echo "Object name must start with an uppercase letter (PascalCase): $OBJECT_NAME" >&2; exit 1 ;;
esac
if printf '%s' "$OBJECT_NAME" | grep -q '[^A-Za-z0-9]'; then
    echo "Object name must be alphanumeric (no '_', '/', '.', spaces): $OBJECT_NAME" >&2
    exit 1
fi
case "$DIRECTORY" in
    ""|/*|*..*) echo "Directory must be a relative path inside src/ (e.g. scene, renderer, utils/units): $DIRECTORY" >&2; exit 1 ;;
esac

# Resolve paths relative to the repo root (parent of this script's dir), so the
# script works no matter what directory you invoke it from.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_DIR="$REPO_ROOT/src/$DIRECTORY/$OBJECT_NAME"
HEADER="$TARGET_DIR/$OBJECT_NAME.h"
SOURCE="$TARGET_DIR/$OBJECT_NAME.cpp"

if [ -e "$HEADER" ] || [ -e "$SOURCE" ]; then
    echo "Refusing to overwrite: $OBJECT_NAME.h/.cpp already exist in $TARGET_DIR" >&2
    exit 1
fi

mkdir -p "$TARGET_DIR"

cat > "$HEADER" <<EOF
#pragma once

namespace swish {

class $OBJECT_NAME {
public:
    $OBJECT_NAME();
    ~$OBJECT_NAME();
};

}  // namespace swish
EOF

cat > "$SOURCE" <<EOF
#include "$OBJECT_NAME.h"

namespace swish {

$OBJECT_NAME::$OBJECT_NAME() = default;
$OBJECT_NAME::~$OBJECT_NAME() = default;

}  // namespace swish
EOF

echo "Created:"
echo "  ${HEADER#"$REPO_ROOT"/}"
echo "  ${SOURCE#"$REPO_ROOT"/}"

# ── Registration ───────────────────────────────────────────────────────
# Sources under src/ are globbed by CMake (file(GLOB_RECURSE ... CONFIGURE_DEPENDS)),
# so no CMakeLists.txt edit is needed; the next build re-globs automatically.
echo
echo "CMake globs src/**/*.cpp - no registration needed. Just run: make build"
