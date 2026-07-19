#!/usr/bin/env bash
# Self-test for the source-registry automation (create_object.sh / prune_sources.sh).
#
# Guards the fragile part: both scripts anchor on the literal `add_executable(swish`
# line and the first `)` that closes it. If that block is ever renamed, wrapped in a
# variable, or split, the awk anchors silently stop matching. This test would catch
# that by asserting a full create -> prune round-trip and byte-identical CMakeLists.txt.
#
# Run locally:  bash scripts/test_source_registry.sh   (also runs in CI)
# Exit 0 = anchors intact; non-zero = automation broke.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE="$REPO_ROOT/CMakeLists.txt"
CREATE="$REPO_ROOT/scripts/create_object.sh"
PRUNE="$REPO_ROOT/scripts/prune_sources.sh"

# A name unlikely to collide with any real module.
PROBE_DIR="scene"
PROBE_NAME="RegistrySelfTestProbe"
PROBE_SRC="src/$PROBE_DIR/$PROBE_NAME/$PROBE_NAME.cpp"
PROBE_MODDIR="$REPO_ROOT/src/$PROBE_DIR/$PROBE_NAME"

fail() { echo "FAIL: $1" >&2; exit 1; }

# Snapshot the pristine CMakeLists so we can prove a clean round-trip and restore
# it no matter how the test exits.
SNAPSHOT="$(mktemp)"
cp "$CMAKE" "$SNAPSHOT"

cleanup() {
    # Always restore CMakeLists and remove any probe files, even on failure.
    cp "$SNAPSHOT" "$CMAKE"
    rm -f "$PROBE_MODDIR/$PROBE_NAME.h" "$PROBE_MODDIR/$PROBE_NAME.cpp" 2>/dev/null || true
    rmdir "$PROBE_MODDIR" 2>/dev/null || true
    rm -f "$SNAPSHOT"
}
trap cleanup EXIT

# Guard against a stale probe from a previously-killed run.
[ -e "$PROBE_MODDIR" ] && fail "probe dir already exists: $PROBE_MODDIR (clean it up first)"

echo "1/4  create_object.sh registers the .cpp in the swish target"
bash "$CREATE" "$PROBE_DIR" "$PROBE_NAME" >/dev/null
grep -qF "$PROBE_SRC" "$CMAKE" \
    || fail "create_object.sh did not insert '$PROBE_SRC' — the add_executable(swish) anchor may have moved."
[ -f "$PROBE_MODDIR/$PROBE_NAME.cpp" ] && [ -f "$PROBE_MODDIR/$PROBE_NAME.h" ] \
    || fail "scaffolded files missing."

echo "2/4  the inserted line is INSIDE the swish block (before its closing paren)"
# Extract the swish block and confirm the probe line is within it.
awk '/^add_executable\(swish/{f=1} f{print} f&&/^\)/{exit}' "$CMAKE" | grep -qF "$PROBE_SRC" \
    || fail "inserted source landed outside the add_executable(swish) block."

echo "3/4  prune_sources.sh --check flags the source once its files are deleted"
rm -f "$PROBE_MODDIR/$PROBE_NAME.h" "$PROBE_MODDIR/$PROBE_NAME.cpp"
rmdir "$PROBE_MODDIR"
if bash "$PRUNE" --check >/dev/null 2>&1; then
    fail "prune --check should have exited non-zero for a missing source."
fi

echo "4/4  make prune removes it and CMakeLists.txt round-trips byte-identical"
bash "$PRUNE" >/dev/null
grep -qF "$PROBE_SRC" "$CMAKE" && fail "prune did not remove the dead source line."
diff -u "$SNAPSHOT" "$CMAKE" \
    || fail "CMakeLists.txt did not round-trip to its original bytes after create+prune."

echo "PASS: source-registry automation intact (anchors match, round-trip clean)."
