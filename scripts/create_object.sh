#!/usr/bin/env bash
# Scaffold a new swish module: creates src/<dir>/<Name>/<Name>.h and <Name>.cpp
# with a header guard, the swish namespace, and a matching include — mirroring
# the existing module layout (see src/scene/Camera/).
#
# Usage:   scripts/create_object.sh <dir-under-src> <ObjectName>
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

# ── Register the .cpp in the swish target's source list ────────────────
# Insert "    src/<dir>/<Name>/<Name>.cpp" as the last entry before the ")"
# that closes the `add_executable(swish ...)` block. Only the .cpp is added;
# headers aren't compilation units and aren't listed in the block today.
CMAKE="$REPO_ROOT/CMakeLists.txt"
REL_SOURCE="src/$DIRECTORY/$OBJECT_NAME/$OBJECT_NAME.cpp"

if [ ! -f "$CMAKE" ]; then
    echo
    echo "Warning: $CMAKE not found — add $REL_SOURCE to the build manually." >&2
elif grep -qF "$REL_SOURCE" "$CMAKE"; then
    echo
    echo "Already listed in CMakeLists.txt: $REL_SOURCE"
else
    # awk: within the add_executable(swish ...) block, remember the last line;
    # when the closing ")" arrives, emit the new source line before it.
    tmp="$(mktemp)"
    awk -v newsrc="    $REL_SOURCE" '
        /^add_executable\(swish/ { inblock = 1 }
        inblock && /^\)/         { print newsrc; inblock = 0 }
        { print }
    ' "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"
    echo
    echo "Added to CMakeLists.txt (swish target): $REL_SOURCE"
    echo "Reconfigure to pick it up:  make build   (or your usual configure step)"
fi
