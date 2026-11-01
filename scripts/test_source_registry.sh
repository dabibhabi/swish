#!/usr/bin/env bash
# Self-test for source discovery. Sources are globbed (not hand-listed), so this
# now asserts that CMakeLists.txt still globs src/ and shaders/ with CONFIGURE_DEPENDS
# and that no hand-maintained add_executable(swish ...) source list has crept back in.
# Run locally: bash scripts/test_source_registry.sh
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE="$REPO_ROOT/CMakeLists.txt"
fail() { echo "FAIL: $1" >&2; exit 1; }

grep -qE 'file\(GLOB_RECURSE SWISH_SOURCES CONFIGURE_DEPENDS' "$CMAKE" || fail "swish source glob missing"
grep -qE 'file\(GLOB SHADER_SOURCES CONFIGURE_DEPENDS' "$CMAKE" || fail "shader glob missing"
if grep -qE '^\s+src/.*\.cpp\s*$' "$CMAKE"; then fail "hand-listed .cpp found in CMakeLists.txt"; fi
echo "PASS: sources and shaders are globbed with CONFIGURE_DEPENDS."
