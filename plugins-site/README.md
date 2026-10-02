# The plugins' download site

A static site for FRACTURE and CRATE: what each one does, a screenshot of each panel, the
Mac and Windows downloads, how to install, and the two built-in guides as web pages. FRACTURE's browser
version is included too, so people can try it before they download anything.

```
npm run site:plugins          # builds plugins-site/public/
npm run test:plugins-site     # builds it three ways and reads it in a real browser
```

Open `plugins-site/public/index.html` to preview it. It works straight from disk.

## What goes into it

| From | Becomes |
|---|---|
| `src/index.html`, `src/site.css` | the page |
| `img/fracture.png`, `img/crate.png` | the screenshots, rendered by each plugin's `host_smoke` |
| `plugin/GUIDE.md`, `crate/GUIDE.md` | `guide-fracture.html`, `guide-crate.html`. These are the files the plugins show behind Guide, so the site and the plugin say the same thing |
| `fx/fracture.html` | `play/fracture.html`, copied as-is |
| `plugin/CMakeLists.txt`, `crate/CMakeLists.txt` | the version numbers on the page |
| `site.json` | the title, your name, a contact address, and download links |
| `headers.mjs` | the response headers, written out as `vercel.json` |

The page runs no scripts and loads nothing from anywhere else. The only page with code
is the browser version.

## Where the downloads come from

For each plugin, the build uses the first of these that exists:

1. **A URL in `site.json`**, for a file hosted somewhere else, one per system:
   `"downloads": { "crate": { "macOS": "https://…/CRATE-0.1.0-macOS.dmg", "Windows": "https://…/Fracture-and-Crate-0.1.0-Windows-Setup.exe" } }`
2. **A disk image on this machine**: the newest `FRACTURE-<version>-macOS.dmg` or
   `CRATE-<version>-macOS.dmg` in `plugins-site/downloads/`, or wherever
   `./build-macos-all.sh --dmg` leaves them (`plugin/dist/`, `crate/dist/`). The build copies it
   into the site and prints its size and SHA-256 on the page, so people can check the file.
   For Windows, the newest `Fracture-and-Crate-<version>-Windows-Setup.exe` (or
   `<NAME>-<version>-Windows-Setup.exe`) there or in `dist/`, where
   `.\build-windows.ps1 -Package` leaves it.
3. Otherwise the button says **coming soon**, and the build warns you.

`--require-downloads` (`npm run site:plugins -- --require-downloads`) turns that warning into
a failure. Use it before you publish.

## Publishing it

`site.json` already points both Download buttons at the v0.2.0 release, which CI built
(`Fracture-and-Crate-0.2.0-macOS.dmg`, one image holding both plugins). That link only
works for the public once **the repository is public**. While it is private, visitors get a
404 from GitHub.

### Render

New, then **Blueprint**, then pick this repository. `render.yaml` at the top sets the build
command, the output folder and the headers. That's all.

### Vercel

Add New, then **Project**, then import this repository, and set **Root Directory** to
`plugins-site`. `plugins-site/vercel.json` sets the rest.

Both hosts rebuild the site on every push to `main`.

### A new version

1. Bump `VERSION` in `plugin/CMakeLists.txt` and `crate/CMakeLists.txt`.
2. In Actions, open **Build both plugins for macOS**, choose **Run workflow**, and put the
   new tag (for example `v0.2.0`) in **release**. Pushing a `v*` tag does the same.
3. Change the two links in `site.json` to the new file, then push. The host redeploys.

### Without a release

On a Mac, `./build-macos-all.sh --dmg && npm run site:plugins -- --require-downloads`
builds the site around local disk images, with their size and checksum on the page (clear
the links in `site.json` first, since a link wins over a local file). Then
`npx vercel deploy plugins-site/public --prod` uploads the lot.

## Before you post it publicly

- **The JUCE licence.** JUCE 8 is licensed either under the AGPLv3, which means publishing
  the source (a public repository under the AGPL does that), or under JUCE's own licence.
  Decide which before you hand binaries to the public. The splash screen is on in these
  builds.
- **Unsigned builds.** Without an Apple Developer ID the Mac shows an "unidentified
  developer" warning. The page explains the right-click, Open step. Signing and notarising
  would remove the warning.
- **A contact.** Fill in `contact` in `site.json` if you want feedback to reach you. Without
  it, the page asks people to reply to whoever sent them the link.

## Updating the screenshots

They are the PNGs each plugin's host-level test renders. On a large virtual screen they come
out at full design size:

```
xvfb-run -a -s "-screen 0 3840x2160x24" build-frac/host_smoke_artefacts/Release/host_smoke shots/fracture.png
xvfb-run -a -s "-screen 0 3840x2160x24" build-crate/host_smoke_artefacts/Release/host_smoke shots/crate.png
```

The site uses `fracture-filter.png` (it shows the notch in the filter display) as
`img/fracture.png`, and `crate.png` as it is.
