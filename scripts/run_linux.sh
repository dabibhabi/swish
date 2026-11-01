#!/usr/bin/env bash
# run_linux.sh - launch swish on Linux (Arch / Wayland / NVIDIA). Twin of scripts/run_mac.sh
# (which lives on the Arch-side commit 26560d9 and is not in every checkout). Same build
# trees as the Makefile: build/ (release, debug UI off) and build-debug/ (debug UI on).
#
#   scripts/run_linux.sh                 build if missing, run release build
#   scripts/run_linux.sh --debug-ui      run the Dear ImGui build (build-debug/)
#   scripts/run_linux.sh --validation    inject VK_LAYER_KHRONOS_validation + synchronization
#                                        validation through the Vulkan loader. Needed because both
#                                        Release and RelWithDebInfo define NDEBUG, which turns the
#                                        in-app validation off (VulkanContext.cpp).
#   scripts/run_linux.sh --x11           force XWayland (unset WAYLAND_DISPLAY) if the native
#                                        Wayland window/cursor capture misbehaves
#   scripts/run_linux.sh --prime         hybrid laptop: render on the NVIDIA GPU
#   scripts/run_linux.sh --dry-run       print the environment + command, run nothing
#
# Needs: cmake, ninja or make, a C++20 compiler, glfw, glm, vulkan headers/loader, glslc
# (Arch: pacman -S cmake ninja glfw glm vulkan-headers vulkan-icd-loader shaderc vulkan-validation-layers)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DEBUG_UI=0; VALIDATION=0; X11=0; PRIME=0; DRY=0
for arg in "$@"; do
    case "$arg" in
        --debug-ui) DEBUG_UI=1 ;;
        --validation) VALIDATION=1 ;;
        --x11) X11=1 ;;
        --prime) PRIME=1 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "run_linux.sh: unknown option $arg" >&2; exit 2 ;;
    esac
done

if [ "$DEBUG_UI" -eq 1 ]; then
    BUILD_DIR=build-debug; BUILD_TYPE=RelWithDebInfo; UI=ON
else
    BUILD_DIR=build; BUILD_TYPE=Release; UI=OFF
fi
BIN="$BUILD_DIR/swish"
JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

if [ ! -x "$BIN" ] && [ "$DRY" -eq 0 ]; then
    echo "swish not built yet - configuring + building $BUILD_TYPE into $BUILD_DIR/ ..."
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DSWISH_DEBUG_UI="$UI" -DSWISH_BACKEND=LINUX_VULKAN
    cmake --build "$BUILD_DIR" -j"$JOBS"
fi

ENVV=()
if [ "$VALIDATION" -eq 1 ]; then
    ENVV+=("VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation")            # older loaders
    ENVV+=("VK_LOADER_LAYERS_ENABLE=*validation*")                       # loader >= 1.3.234
    ENVV+=("VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT")
fi
if [ "$PRIME" -eq 1 ]; then
    ENVV+=("__NV_PRIME_RENDER_OFFLOAD=1" "__VK_LAYER_NV_optimus=NVIDIA_only" "__GLX_VENDOR_LIBRARY_NAME=nvidia")
fi

CMD=(env)
if [ "$X11" -eq 1 ]; then CMD+=(-u WAYLAND_DISPLAY); fi
if [ "${#ENVV[@]}" -gt 0 ]; then CMD+=("${ENVV[@]}"); fi
CMD+=("./$BIN")

if [ "$DRY" -eq 1 ]; then
    echo "cwd: $REPO_ROOT"
    printf '%q ' "${CMD[@]}"; echo
    exit 0
fi
exec "${CMD[@]}"
