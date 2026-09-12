#!/bin/bash
# Installs FRACTURE and CRATE for the current user. Double-click this from the
# disk image.
#
# If macOS refuses to run it ("cannot be opened because it is from an
# unidentified developer"), right-click it and choose Open, then Open again in
# the dialog. That is macOS asking whether you trust whoever sent you this, not
# a sign that anything is wrong.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
components="$HOME/Library/Audio/Plug-Ins/Components"
vst3="$HOME/Library/Audio/Plug-Ins/VST3"

printf '\n  FRACTURE  ·  multi-band multi-fx distortion\n'
printf '  CRATE     ·  twelve-bit drum processor\n\n'
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
        printf '  – %s is not in this disk image, skipping\n' "$what"
        return
    fi
    rm -rf "$dest/$(basename "$src")"
    # ditto, not cp: it is the copy that keeps bundle metadata intact
    if ditto "$src" "$dest/$(basename "$src")"; then
        # the step that matters. macOS marks anything downloaded as quarantined,
        # and a quarantined plug-in is refused SILENTLY by the host — Logic
        # simply does not list it, with no error to explain why
        xattr -dr com.apple.quarantine "$dest/$(basename "$src")" 2>/dev/null
        printf '  ✓ %s\n' "$what"
    else
        printf '  ✗ %s could not be copied\n' "$what"
    fi
}

for name in FRACTURE CRATE; do
    install_bundle "$here/Plug-ins/$name.component" "$components" "$name  Audio Unit"
    install_bundle "$here/Plug-ins/$name.vst3" "$vst3" "$name  VST3"
done

printf '\n  validating the Audio Units the way Logic will…\n\n'
killall -9 AudioComponentRegistrar >/dev/null 2>&1
fail=0
validate() {
    local name="$1" code="$2"
    if auval -v aufx "$code" Frct >"/tmp/$name-auval.txt" 2>&1; then
        printf '  ✓ %s passed\n' "$name"
    else
        printf '  ✗ %s failed validation — full log at /tmp/%s-auval.txt\n' "$name" "$name"
        tail -4 "/tmp/$name-auval.txt" | sed 's/^/      /'
        fail=1
    fi
}
validate FRACTURE Frcd
validate CRATE Crt1

printf '\n'
if [[ $fail -eq 0 ]]; then
    printf '  Done. Open Logic and look under Audio Units:\n'
    printf '      Fracture → Distortion → FRACTURE\n'
    printf '      Crate → Distortion → CRATE\n'
else
    printf '  Logic will not load a plugin that fails validation. If it says the\n'
    printf '  component was not found, run:  killall -9 AudioComponentRegistrar\n'
    printf '  and try this installer again.\n'
fi

printf '\n  To uninstall, delete these:\n'
printf '    %s/FRACTURE.component  and  %s/CRATE.component\n' "$components" "$components"
printf '    %s/FRACTURE.vst3  and  %s/CRATE.vst3\n\n' "$vst3" "$vst3"
read -n 1 -s -r -p "  Press any key to close."
printf '\n'
