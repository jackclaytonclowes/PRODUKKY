#!/usr/bin/env bash
# Builds FRACTURE for macOS — Audio Unit (for Logic), VST3, and a standalone app —
# and installs them where the hosts look. Run it from anywhere:
#
#   ./plugin/build-macos.sh            build and install for yourself
#   ./plugin/build-macos.sh --dmg      ... and pack a disk image to send people
#
# What you need first: Xcode command line tools (`xcode-select --install`) and
# CMake (`brew install cmake`, or the CMake app plus "Install command line
# tools"). Everything else, JUCE included, is fetched by the build.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script builds the Audio Unit, which only exists on macOS."
    echo "On Linux or Windows, build the VST3 instead:"
    echo "    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Fracture_VST3 -j"
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode command line tools are missing. Run:  xcode-select --install"
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is missing. Install it with:  brew install cmake"
    echo "(or download the CMake app and choose 'Install Command Line Tools')"
    exit 1
fi

# Apple Silicon and Intel in one binary, so the same build works on either Mac
# and inside Rosetta hosts. Override with ARCHS=arm64 for a faster build.
ARCHS="${ARCHS:-arm64;x86_64}"

echo "==> configuring (fetching JUCE on the first run, this takes a few minutes)"
cmake -B build-macos \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

echo "==> building"
cmake --build build-macos --config Release \
      --target Fracture_AU Fracture_VST3 Fracture_Standalone \
      -j "$(sysctl -n hw.ncpu)"

artefacts="build-macos/Fracture_artefacts/Release"
echo
echo "==> built"
ls -d "$artefacts"/AU/*.component "$artefacts"/VST3/*.vst3 "$artefacts"/Standalone/*.app 2>/dev/null || true

# juce_add_plugin copies into ~/Library/Audio/Plug-Ins on a successful build, but
# say so plainly rather than leaving it to be discovered
echo
echo "==> installed for this user"
ls -d ~/Library/Audio/Plug-Ins/Components/FRACTURE.component 2>/dev/null || echo "   (no AU found — check the build output above)"
ls -d ~/Library/Audio/Plug-Ins/VST3/FRACTURE.vst3 2>/dev/null || true

if [[ "${1:-}" == "--dmg" ]]; then
    echo
    echo "==> packing a disk image"
    "$here/packaging/make-dmg.sh"
fi

echo
echo "==> validating the Audio Unit the way Logic will"
if auval -v aufx Frcd Frct; then
    echo
    echo "PASSED. Restart Logic and look for FRACTURE under Audio Units > Fracture > Distortion."
else
    echo
    echo "auval failed. Logic will refuse to load the plugin until that passes."
    echo "If it says the component was not found, run:  killall -9 AudioComponentRegistrar"
    echo "then try auval again."
    exit 1
fi
