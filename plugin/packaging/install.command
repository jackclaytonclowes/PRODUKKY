#!/bin/bash
# Installs FRACTURE for the current user. Double-click this from the disk image.
#
# If macOS refuses to run it ("cannot be opened because it is from an
# unidentified developer"), right-click it and choose Open, then Open again in
# the dialog. That is macOS asking whether you trust whoever sent you this, not
# a sign anything is wrong.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
components="$HOME/Library/Audio/Plug-Ins/Components"
vst3="$HOME/Library/Audio/Plug-Ins/VST3"

printf '\n  FRACTURE — multi-band multi-fx distortion\n'
printf '  installing for %s\n\n' "$USER"

if pgrep -x "Logic Pro" >/dev/null 2>&1 || pgrep -x "Logic Pro X" >/dev/null 2>&1; then
    printf '  Logic is running. Quit it first, then run this again.\n\n'
    read -n 1 -s -r -p "  Press any key to close."
    exit 1
fi

mkdir -p "$components" "$vst3"

install_bundle() {
    local src="$1" dest="$2" what="$3"
    if [[ ! -d "$src" ]]; then
        printf '  – %s not in this disk image, skipping\n' "$what"
        return
    fi
    rm -rf "$dest/$(basename "$src")"
    # ditto, not cp: it is the copy that keeps bundle metadata intact
    if ditto "$src" "$dest/$(basename "$src")"; then
        # the one step that matters. macOS marks anything downloaded as
        # quarantined, and a quarantined plug-in is silently refused by the host
        xattr -dr com.apple.quarantine "$dest/$(basename "$src")" 2>/dev/null
        printf '  ✓ %s → %s\n' "$what" "$dest"
    else
        printf '  ✗ %s could not be copied\n' "$what"
    fi
}

install_bundle "$here/FRACTURE.component" "$components" "Audio Unit"
install_bundle "$here/FRACTURE.vst3" "$vst3" "VST3"

printf '\n  validating the Audio Unit the way Logic will…\n\n'
killall -9 AudioComponentRegistrar >/dev/null 2>&1
if auval -v aufx Frcd Frct >/tmp/fracture-auval.txt 2>&1; then
    printf '  ✓ passed. Open Logic and look under Audio Units → Fracture → Distortion.\n'
else
    printf '  ✗ validation failed. The full log is at /tmp/fracture-auval.txt\n'
    printf '    Logic will not load the plugin until this passes.\n'
    tail -5 /tmp/fracture-auval.txt | sed 's/^/    /'
fi

printf '\n  To uninstall, delete these:\n'
printf '    %s/FRACTURE.component\n' "$components"
printf '    %s/FRACTURE.vst3\n\n' "$vst3"
read -n 1 -s -r -p "  Press any key to close."
printf '\n'
