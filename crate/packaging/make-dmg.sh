#!/usr/bin/env bash
# Packs the built plugins into a disk image people can be sent.
#
#   ./plugin/packaging/make-dmg.sh                 # after ./plugin/build-macos.sh
#   ./plugin/packaging/make-dmg.sh path/to/Release
#
# macOS only: hdiutil and Finder do the work. The result is
# crate/dist/CRATE-<version>-macOS.dmg.
set -euo pipefail
# say where it stopped: hdiutil -quiet closes its own output, so without this a
# failure is a bare exit code
trap 'echo "make-dmg.sh stopped at line $LINENO (exit $?)" >&2' ERR

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "A .dmg can only be built on macOS — hdiutil and Finder do the work."
    exit 1
fi

artefacts="${1:-$root/build-macos/Crate_artefacts/Release}"
version="$(sed -n 's/^project(CRATE VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
version="${version:-0.1.0}"
volume="CRATE $version"
out="$root/dist/CRATE-$version-macOS.dmg"

au="$artefacts/AU/CRATE.component"
vst3="$artefacts/VST3/CRATE.vst3"
app="$artefacts/Standalone/CRATE.app"
[[ -d "$au" ]] || { echo "No Audio Unit at $au — build it first with ./plugin/build-macos.sh"; exit 1; }

echo "==> staging $volume"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
ditto "$au" "$stage/CRATE.component"
[[ -d "$vst3" ]] && ditto "$vst3" "$stage/CRATE.vst3"
[[ -d "$app"  ]] && ditto "$app"  "$stage/CRATE.app"
cp "$here/install.command" "$stage/Install CRATE.command"
chmod +x "$stage/Install CRATE.command"
cp "$here/READ ME FIRST.txt" "$stage/READ ME FIRST.txt"
cp "$here/../GUIDE.md" "$stage/How it works.md"      # the same guide the Guide button shows
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
# hdiutil create now and then fails with "Resource busy" on a busy machine
# (GitHub's macOS runners do it often), so it gets three tries, with its errors
# shown rather than swallowed by -quiet
made=0
for attempt in 1 2 3; do
    if hdiutil create -srcfolder "$stage" -volname "$volume" -fs HFS+ \
            -format UDRW -ov "$root/dist/.tmp.dmg" >/dev/null; then
        made=1; break
    fi
    echo "    hdiutil create failed (try $attempt of 3); waiting and trying again" >&2
    sleep 5
done
[[ $made -eq 1 ]] || { echo "Could not create the disk image." >&2; exit 1; }

# keep attach's output, so a failure can print what it said, then take the
# first /dev line (the whole disk, which is what detach wants)
attached="$(hdiutil attach -readwrite -noverify -noautoopen "$root/dist/.tmp.dmg")"
device="$(printf '%s\n' "$attached" | awk '/^\/dev\// && !d { d = $1 } END { print d }')"
[[ -n "$device" ]] || { echo "Could not attach the disk image:" >&2; echo "$attached" >&2; exit 1; }
mount="/Volumes/$volume"
sleep 2

echo "==> arranging the window"
osascript <<APPLESCRIPT || echo "    (Finder would not arrange the window; the image is still fine)"
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
        set position of item "Install CRATE.command" of container window to {196, 200}
        set position of item "READ ME FIRST.txt" of container window to {530, 200}
        set position of item "How it works.md" of container window to {363, 200}
        set position of item "CRATE.component" of container window to {110, 360}
        try
            set position of item "CRATE.vst3" of container window to {250, 360}
        end try
        try
            set position of item "CRATE.app" of container window to {400, 360}
        end try
        set position of item "Applications" of container window to {600, 360}
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
echo "It is unsigned, so whoever opens it will have to right-click the installer"
echo "and choose Open the first time. READ ME FIRST.txt says so in plain words."
