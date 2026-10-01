# FRACTURE and CRATE

Two audio effect plugins, built with JUCE as Audio Unit, VST3 and a standalone app.

- **FRACTURE** (`plugin/`) is a multi-band, multi-FX distortion after Output's Thermal and
  Minimal Audio's Rift. It splits the sound into up to three bands, each with two drive
  stages and fifteen modes. It also has a crusher, feedback that can ring at a note, an
  analogue-style filter with a rhythm and a drawn response, a modulation matrix, an XY pad
  and macros, a Table mode whose harmonics you draw, and mid/side drive per band. It has
  49 presets.
- **CRATE** (`crate/`) is a twelve-bit drum processor after the SP-1200 and Akai S900: the
  two machines' converters, the pitch trick, a hit-opened four-pole filter, a rhythm, dust,
  and swing against the host's grid, with the low end kept clean if you want it. It has 27 presets.
- **The browser version** of FRACTURE (`fx/fracture.html`) is one HTML file: the plugin's own
  engine, compiled to WebAssembly, behind the plugin's own panel. It is also where the plugin's
  first presets and shapers come from.
- **The download site** (`plugins-site/`) is a static site with both plugins, their guides
  and the downloads, ready for Vercel or Render.

## Building

On a Mac, with Xcode's command line tools and CMake:

```
./build-macos-all.sh              # build, install into your plug-in folders, validate
./build-macos-all.sh --dmg        # ... and pack a disk image of each
```

Elsewhere, the VST3s:

```
cmake -B plugin/build -S plugin && cmake --build plugin/build --target Fracture_VST3 -j
cmake -B crate/build  -S crate  && cmake --build crate/build  --target Crate_VST3 -j
```

`.github/workflows/macos.yml` does the Mac build on GitHub. Run it from the Actions tab (fill
in "release" with a tag such as `v0.1.2` to publish it), or push a `v*` tag.

## Testing

```
npm install
npm test                 # everything below that needs no JUCE
npm run test:core        # FRACTURE's DSP, with a bare C++ compiler
npm run test:crate       # CRATE's DSP, the same way
npm run test:fx          # the browser version, in headless Chromium
npm run test:plugins-site  # the download site, built and read in Chromium
npm run audition         # render every preset over a loop, to listen without a DAW
```

Each product also has `host_smoke`, a host-level test that needs the JUCE build. It
instantiates the plugin, walks the presets, round-trips state, drives the editor with mouse
events and renders it to a PNG. `plugin/README.md` and `crate/README.md` explain what every
test measures, what has not been verified, and why.

## Layout

```
CHANGELOG.md     what changed in each version
ROADMAP.md       what is next, and what could sit alongside these two
plugin/          FRACTURE: core/ (JUCE-free DSP), Source/ (wrapper and editor), tests/, packaging/
crate/           CRATE, laid out the same way
fx/              the browser version and its design sources
plugins-site/    the download site (render.yaml at the top deploys it on Render)
tools/audition/  renders presets to WAV with the real DSP
packaging/       one disk image holding both plugins (what CI builds)
scripts/lib/     a small PNG writer, for the disk image backgrounds and the site's icon
tests/           the browser version's and the site's suites
```
