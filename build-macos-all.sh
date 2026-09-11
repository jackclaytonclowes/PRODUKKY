#!/usr/bin/env bash
# Builds and installs BOTH plugins for Logic: FRACTURE (multi-band distortion)
# and CRATE (twelve-bit drum processor).
#
#   ./build-macos-all.sh              build, install, validate
#   ./build-macos-all.sh --dmg        ... and pack a disk image for each
#   COMPANY="Your Name" ./build-macos-all.sh     put both under one maker in Logic
#
# What you need first, and nothing else:
#   xcode-select --install     Apple's command line tools
#   brew install cmake         (or the CMake app, then "Install Command Line Tools")
#
# JUCE is fetched once here and shared between the two builds, rather than
# downloaded twice.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Audio Units only exist on macOS. On Linux or Windows build the VST3s:"
    echo "    cmake -B plugin/build -S plugin && cmake --build plugin/build --target Fracture_VST3 -j"
    echo "    cmake -B crate/build  -S crate  && cmake --build crate/build  --target Crate_VST3 -j"
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode command line tools are missing. Run:  xcode-select --install"
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is missing. Install it with:  brew install cmake"
    exit 1
fi

# ---- one JUCE for both builds
JUCE_TAG="${JUCE_TAG:-8.0.15}"
if [[ -z "${JUCE_PATH:-}" ]]; then
    JUCE_PATH="$here/.juce"
    if [[ ! -f "$JUCE_PATH/CMakeLists.txt" ]]; then
        echo "==> fetching JUCE $JUCE_TAG (once, about 200 MB)"
        rm -rf "$JUCE_PATH"
        git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git "$JUCE_PATH" \
            || { echo "Could not fetch JUCE. Check your network and try again."; exit 1; }
    else
        echo "==> using the JUCE already in .juce"
    fi
fi
export JUCE_PATH

status=0
build_one() {
    local dir="$1" name="$2"
    shift 2
    echo
    echo "============================================================"
    echo "  $name"
    echo "============================================================"
    if ! "./$dir/build-macos.sh" "$@"; then
        echo "  !! $name did not finish cleanly (see above)"
        status=1
    fi
}

flags=()
[[ "${1:-}" == "--dmg" ]] && flags+=(--dmg)
build_one plugin FRACTURE ${flags[@]+"${flags[@]}"}
build_one crate  CRATE    ${flags[@]+"${flags[@]}"}

echo
echo "============================================================"
if [[ $status -eq 0 ]]; then
    echo "  both installed. Restart Logic, then look in"
    echo "     Audio Units > Fracture > Distortion   (FRACTURE)"
    echo "     Audio Units > Crate > Distortion      (CRATE)"
else
    echo "  one of the two had a problem — scroll up for which"
fi
echo "============================================================"
ls -d ~/Library/Audio/Plug-Ins/Components/*.component 2>/dev/null
ls -d ~/Library/Audio/Plug-Ins/VST3/*.vst3 2>/dev/null
echo
echo "If Logic does not list them, quit Logic and run:"
echo "    killall -9 AudioComponentRegistrar"
echo "then start it again. Both are unsigned personal builds, which is fine on"
echo "your own machine; sending them to anyone else needs the disk image"
echo "(--dmg) so they get the installer that clears the quarantine flag."
exit $status
