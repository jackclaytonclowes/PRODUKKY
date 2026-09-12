#!/usr/bin/env bash
# Packs BOTH plugins into one disk image, so installing the pair is one download
# and one double-click.
#
#   ./build-macos-all.sh          # build them first
#   ./packaging/make-dmg.sh       # then this
#
# macOS only: hdiutil and Finder do the work. The result is
# dist/Fracture-and-Crate-<version>-macOS.dmg.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "A .dmg can only be built on macOS — hdiutil and Finder do the work."
    exit 1
fi

version="$(sed -n 's/^project(CRATE VERSION \([0-9.]*\).*/\1/p' "$root/crate/CMakeLists.txt")"
version="${version:-0.1.0}"
volume="Fracture and Crate $version"
out="$root/dist/Fracture-and-Crate-$version-macOS.dmg"

fracture="$root/plugin/build-macos/Fracture_artefacts/Release"
crate="$root/crate/build-macos/Crate_artefacts/Release"
[[ -d "$fracture/AU/FRACTURE.component" ]] || { echo "FRACTURE is not built — run ./build-macos-all.sh"; exit 1; }
[[ -d "$crate/AU/CRATE.component" ]] || { echo "CRATE is not built — run ./build-macos-all.sh"; exit 1; }

echo "==> staging $volume"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/Plug-ins" "$stage/Standalone apps"
for pair in "$fracture:FRACTURE" "$crate:CRATE"; do
    dir="${pair%%:*}"; name="${pair##*:}"
    ditto "$dir/AU/$name.component" "$stage/Plug-ins/$name.component"
    [[ -d "$dir/VST3/$name.vst3" ]] && ditto "$dir/VST3/$name.vst3" "$stage/Plug-ins/$name.vst3"
    [[ -d "$dir/Standalone/$name.app" ]] && ditto "$dir/Standalone/$name.app" "$stage/Standalone apps/$name.app"
done
cp "$here/install.command" "$stage/Install both plugins.command"
chmod +x "$stage/Install both plugins.command"
cp "$here/READ ME FIRST.txt" "$stage/READ ME FIRST.txt"
ln -s /Applications "$stage/Applications"

mkdir -p "$stage/.background"
if command -v node >/dev/null 2>&1; then
    node "$here/make-background.mjs" "$stage/.background/background.png" >/dev/null
else
    echo "    (node not found — the disk image will have a plain background)"
fi

echo "==> building the image"
mkdir -p "$root/dist"
rm -f "$out" "$root/dist/.tmp.dmg"
hdiutil create -srcfolder "$stage" -volname "$volume" -fs HFS+ \
        -format UDRW -ov -quiet "$root/dist/.tmp.dmg"

device="$(hdiutil attach -readwrite -noverify -noautoopen "$root/dist/.tmp.dmg" \
          | egrep '^/dev/' | sed 1q | awk '{print $1}')"
sleep 2

# Finder is not running on a build machine, so this step is allowed to fail: the
# image is complete either way, it just opens without the arranged layout
echo "==> arranging the window"
osascript <<APPLESCRIPT || echo "    (no Finder here — the image is fine, just unarranged)"
tell application "Finder"
    tell disk "$volume"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 920, 622}
        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 96
        set text size of opts to 12
        set background picture of opts to file ".background:background.png"
        set position of item "Install both plugins.command" of container window to {196, 200}
        set position of item "READ ME FIRST.txt" of container window to {530, 200}
        set position of item "Plug-ins" of container window to {150, 360}
        set position of item "Standalone apps" of container window to {360, 360}
        set position of item "Applications" of container window to {580, 360}
        update without registering applications
        delay 2
        close
    end tell
end tell
APPLESCRIPT

sync
hdiutil detach "$device" -quiet || hdiutil detach "$device" -force -quiet
hdiutil convert "$root/dist/.tmp.dmg" -format UDZO -imagekey zlib-level=9 -o "$out" -quiet
rm -f "$root/dist/.tmp.dmg"

echo
echo "==> $out"
ls -lh "$out" | awk '{print "    " $5}'
echo
echo "Unsigned, so whoever opens it right-clicks the installer and chooses Open"
echo "the first time. READ ME FIRST.txt says so in plain words."
