#!/usr/bin/env bash
# No-op. The swish target's sources are globbed by CMake
# (file(GLOB_RECURSE ... CONFIGURE_DEPENDS) in CMakeLists.txt), so deleted modules
# no longer leave dead entries behind. Kept so `make prune` / old docs still work.
echo "sources are globbed by CMake (CONFIGURE_DEPENDS); nothing to prune."
exit 0
