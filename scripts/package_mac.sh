#!/usr/bin/env bash
# Builds a macOS TEST bundle of Swish for friends to playtest.
#
#   scripts/package_mac.sh [release|debug]     (default: release; or `make package VARIANT=debug`)
#
#   build/dist/<stage>/Swish.app               release  (SWISH_DEBUG_UI=OFF)
#   build/dist/<stage>/Swish-Debug.app         debug    (SWISH_DEBUG_UI=ON, live-tuning UI)
#   build/dist/<stage>.zip                     what you send
#
# What the bundle carries: the binary, the compiled shaders, road.bin + car.toml, textures, the rigged Porsche and
# the HDRIs and corridor it loads, the MoltenVK driver (from the Vulkan SDK) and the project LICENSE + asset CREDITS.
# Everything it loads is found relative to the executable (src/utils/io/AppPaths.h), so it runs without the repo.
#
# TEST BUILDS ONLY. The bundle is ad-hoc signed (not notarised), so macOS blocks it until the quarantine flag is
# removed (the _READ_ME_FIRST.md inside explains how). Before any public release, read docs/packaging.md: it lists
# what is still missing (Developer ID signing, notarisation, third-party licence notices, the non-commercial Porsche
# licence, excluding the 589 MB Charger folder).
set -euo pipefail

variant="${1:-release}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "$variant" in
    release) build_dir="$root/build";       make_target=build;       app_name="Swish" ;;
    debug)   build_dir="$root/build-debug"; make_target=debug-build; app_name="Swish-Debug" ;;
    *) echo "usage: $0 [release|debug]" >&2; exit 2 ;;
esac

sdk="${VULKAN_SDK:?set VULKAN_SDK to the macOS Vulkan SDK (it supplies libvulkan and MoltenVK)}"
icd_src="$sdk/share/vulkan/icd.d/MoltenVK_icd.json"
[ -f "$icd_src" ] || { echo "missing $icd_src" >&2; exit 1; }

echo "==> building $variant ($make_target)"
make -C "$root" "$make_target"

version="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo dev)"
stage_name="Swish-$variant-$version"
dist="$root/build/dist"
stage="$dist/$stage_name"
app="$stage/$app_name.app"
contents="$app/Contents"
bin="$contents/MacOS/swish"
res="$contents/Resources"
fw="$contents/Frameworks"

echo "==> staging $app"
rm -rf "$stage" "$dist/$stage_name.zip"
mkdir -p "$contents/MacOS" "$res/shaders" "$res/config" "$res/textures" \
         "$res/assets/blend" "$res/assets/Sky" "$res/assets/corridor" "$res/vulkan/icd.d" "$fw"

cp "$build_dir/swish" "$bin"
cp "$build_dir"/shaders/*.spv "$res/shaders/"
cp "$build_dir/config/car.toml" "$build_dir/config/road.bin" "$res/config/"
cp -R "$root/textures/." "$res/textures/"
cp "$root/assets/blend/porsche_rigged.glb" "$res/assets/blend/"
cp -R "$root/assets/Sky/." "$res/assets/Sky/"
cp "$root/assets/corridor/lie_corridor.bin" "$res/assets/corridor/"
cp "$root/LICENSE" "$res/LICENSE.txt"
cp "$root/assets/CREDITS.md" "$res/CREDITS.md"

echo "==> Vulkan driver (MoltenVK) + bundled ICD manifest"
cp -L "$sdk/lib/libMoltenVK.dylib" "$fw/libMoltenVK.dylib"
# The manifest's library_path is relative to the manifest file, as in the SDK's own copy.
sed 's|"library_path": ".*"|"library_path": "../../../Frameworks/libMoltenVK.dylib"|' "$icd_src" \
    > "$res/vulkan/icd.d/MoltenVK_icd.json"

echo "==> making the binary self-contained (dylibs -> Frameworks, @rpath references)"
# LC_RPATH entries of the binary (the SDK lib dir and /opt/homebrew/lib on this machine). @rpath names are resolved
# against them while copying, and then the entries are removed so nothing points outside the bundle.
rpaths="$(otool -l "$bin" | awk '/cmd LC_RPATH/{f=1} f && /path /{print $2; f=0}')"
resolve() {  # on-disk file for an install name
    local dep="$1" dir
    case "$dep" in
        @rpath/*)
            for dir in $rpaths; do
                [ -f "$dir/${dep#@rpath/}" ] && { echo "$dir/${dep#@rpath/}"; return; }
            done
            echo "cannot resolve $dep" >&2; exit 1 ;;
        *) echo "$dep" ;;
    esac
}
for r in $rpaths; do install_name_tool -delete_rpath "$r" "$bin"; done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin"

# Walk the binary and every bundled dylib until no outside reference is left (each copied dylib may pull more).
# The SDK's libvulkan / libMoltenVK are universal (x86_64 + arm64): read only the arm64 slice, or otool prints one
# block per architecture and the header lines get parsed as dependencies (this looped forever once).
changed=1
passes=0
while [ "$changed" = 1 ]; do
    changed=0
    passes=$((passes + 1))
    [ "$passes" -le 20 ] || { echo "dylib rewrite did not converge after 20 passes" >&2; exit 1; }
    for f in "$bin" "$fw"/*.dylib; do
        while IFS= read -r dep; do
            case "$dep" in /usr/lib/*|/System/*|"") continue ;; esac
            base="${dep##*/}"
            if [ ! -e "$fw/$base" ]; then
                cp -L "$(resolve "$dep")" "$fw/$base"
                chmod u+w "$fw/$base"
                install_name_tool -id "@rpath/$base" "$fw/$base"
                changed=1
            fi
            if [ "$dep" != "@rpath/$base" ]; then
                install_name_tool -change "$dep" "@rpath/$base" "$f"
                changed=1
            fi
        done < <(otool -arch arm64 -L "$f" | tail -n +2 | awk '{print $1}')
    done
done

echo "==> Info.plist + ad-hoc signature"
cat > "$contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>        <string>swish</string>
    <key>CFBundleIdentifier</key>        <string>dev.swish.testbuild.$variant</string>
    <key>CFBundleName</key>              <string>$app_name</string>
    <key>CFBundlePackageType</key>       <string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.0.0-test</string>
    <key>CFBundleVersion</key>           <string>$version</string>
    <key>NSPrincipalClass</key>          <string>NSApplication</string>
    <key>NSHighResolutionCapable</key>   <true/>
</dict>
</plist>
PLIST
# Install-name rewrites invalidate the signature, and Apple Silicon refuses to run unsigned code: re-sign inside-out.
for f in "$fw"/*.dylib "$bin"; do codesign --force --sign - "$f" >/dev/null; done
codesign --force --sign - "$app" >/dev/null
codesign --verify --strict "$app"

cp "$root/scripts/friends/_READ_ME_FIRST.md" "$stage/"
echo "==> zipping $stage_name.zip"
# Not ditto: macOS puts com.apple.provenance (SIP-managed, cannot be cleared) on the files, and ditto turns each one into a
# ._* AppleDouble file. A non-Mac unzip then writes those into the bundle and breaks its code seal. zip -X stores no xattrs.
rm -f "$dist/$stage_name.zip"
(cd "$dist" && zip -qrX "$stage_name.zip" "$stage_name")
echo
echo "bundle : $app ($(du -sh "$app" | cut -f1))"
echo "zip    : $dist/$stage_name.zip ($(du -sh "$dist/$stage_name.zip" | cut -f1))"
echo "driver : $(otool -arch arm64 -L "$bin" | grep -c '@rpath') @rpath deps, $(ls "$fw" | wc -l | tr -d ' ') bundled libraries"
echo
echo "==============================================================================="
echo " TEST BUILD ONLY: ad-hoc signed, not notarised. Share with playtesters only."
echo " Before any public release, read docs/packaging.md (the list of what is missing)."
echo "==============================================================================="
