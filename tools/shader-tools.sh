#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODE=${1:?usage: shader-tools.sh validate-gl|validate-gl-project|compile-vulkan|compile-vulkan-project|validate-spirv|validate-spirv-project}
FILE=${2:-}

stage_for() {
    file=$1
    case "$file" in
        *.vert) printf vert;; *.frag) printf frag;; *.geom) printf geom;; *.comp) printf comp;;
        *.tesc) printf tesc;; *.tese) printf tese;;
        *.glsl)
            # Generic GLSL files are ambiguous unless the source declares a stage.
            value=$(sed -En 's/^[[:space:]]*#pragma[[:space:]]+shader_stage[[:space:]]*\([[:space:]]*(vertex|fragment|geometry|compute|tesscontrol|tesseval)[[:space:]]*\).*/\1/p' "$file" | head -n 1)
            case "$value" in vertex) printf vert;; fragment) printf frag;; geometry) printf geom;; compute) printf comp;; tesscontrol) printf tesc;; tesseval) printf tese;; esac
            ;;
    esac
}

has_entry() { rg -q '(^|[[:space:]])void[[:space:]]+main[[:space:]]*\(' "$1"; }

validate_gl_file() {
    file=$1
    stage=$(stage_for "$file" || true)
    if [ -z "$stage" ]; then echo "SKIP (no stage; add #pragma shader_stage(...)): $file"; return 0; fi
    if rg -q 'void[[:space:]]+mainImage[[:space:]]*\(' "$file" && ! has_entry "$file"; then
        echo "SKIP (preview wrapper entry point; Shader Studio validates this shader): $file"; return 0
    fi
    if ! has_entry "$file"; then echo "SKIP (include/source fragment without main): $file"; return 0; fi
    glslangValidator -S "$stage" -I"$ROOT/shaders" -I"$ROOT/shaders/common" "$file"
}

compile_vk_file() {
    file=$1
    stage=$(stage_for "$file" || true)
    if [ -z "$stage" ]; then echo "SKIP (no stage; add #pragma shader_stage(...)): $file"; return 0; fi
    version=$(sed -En 's/^[[:space:]]*#version[[:space:]]+([0-9]+).*/\1/p' "$file" | head -n 1)
    if [ -n "$version" ] && [ "$version" -lt 450 ]; then
        echo "SKIP (OpenGL GLSL; Vulkan task only compiles Vulkan sources): $file"; return 0
    fi
    if ! has_entry "$file"; then echo "SKIP (include/source fragment without main): $file"; return 0; fi
    rel=${file#"$ROOT/"}
    out="$ROOT/build/spirv/${rel}.spv"
    mkdir -p "$(dirname -- "$out")"
    glslc --target-env=vulkan1.2 -I"$ROOT/shaders" -I"$ROOT/shaders/common" "$file" -o "$out" || return $?
    echo "Wrote $out"
}

collect() {
    find "$1" -type f \( -name '*.vert' -o -name '*.frag' -o -name '*.geom' -o -name '*.comp' -o -name '*.glsl' \) -print
}

case "$MODE" in
    validate-gl) [ -n "$FILE" ] || { echo 'Open a shader file first.' >&2; exit 2; }; validate_gl_file "$FILE" ;;
    validate-gl-project)
        count=0
        while IFS= read -r file; do validate_gl_file "$file" || exit; count=$((count + 1)); done <<EOF_FILES
$(collect "$ROOT/shader-playground/shaders")
EOF_FILES
        [ "$count" -gt 0 ] || { echo 'No shader sources found.' >&2; exit 1; }
        ;;
    compile-vulkan) [ -n "$FILE" ] || { echo 'Open a shader file first.' >&2; exit 2; }; compile_vk_file "$FILE" ;;
    compile-vulkan-project)
        count=0
        while IFS= read -r file; do compile_vk_file "$file" || exit; count=$((count + 1)); done <<EOF_FILES
$(collect "$ROOT/shaders")
EOF_FILES
        [ "$count" -gt 0 ] || { echo 'No shader sources found.' >&2; exit 1; }
        ;;
    validate-spirv)
        [ -n "$FILE" ] || { echo 'Open a shader file first.' >&2; exit 2; }
        rel=${FILE#"$ROOT/"}; binary="$ROOT/build/spirv/${rel}.spv"
        [ -f "$binary" ] || { echo "Missing $binary; run Compile current Vulkan GLSL first." >&2; exit 1; }
        spirv-val --target-env vulkan1.2 "$binary" ;;
    validate-spirv-project)
        found=0
        find "$ROOT/build/spirv" -type f -name '*.spv' -print | while IFS= read -r binary; do
            spirv-val --target-env vulkan1.2 "$binary" || exit
            echo "$binary"
        done > "$ROOT/build/spirv/.validated-list"
        found=$(wc -l < "$ROOT/build/spirv/.validated-list" | tr -d ' ')
        rm -f "$ROOT/build/spirv/.validated-list"
        [ "$found" -gt 0 ] || { echo 'No generated SPIR-V files found; compile project shaders first.' >&2; exit 1; }
        echo "Validated $found SPIR-V module(s)." ;;
    *) echo "Unknown mode: $MODE" >&2; exit 2 ;;
esac
