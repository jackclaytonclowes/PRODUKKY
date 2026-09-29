# The plugins' download site

A static site for FRACTURE and CRATE: what each one does, a screenshot of each panel, the
Mac downloads, how to install, and the two built-in guides as web pages. FRACTURE's browser
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

1. **A URL in `site.json`**, for a disk image hosted somewhere else:
   `"downloads": { "crate": { "macOS": "https://…/CRATE-0.1.0-macOS.dmg" } }`
2. **A disk image on this machine**: the newest `FRACTURE-<version>-macOS.dmg` or
   `CRATE-<version>-macOS.dmg` in `plugins-site/downloads/`, or wherever
   `./build-macos-all.sh --dmg` leaves them (`plugin/dist/`, `crate/dist/`). The build copies it
   into the site and prints its size and SHA-256 on the page, so people can check the file.
3. Otherwise the button says **coming soon**, and the build warns you.

`--require-downloads` (`npm run site:plugins -- --require-downloads`) turns that warning into
a failure. Use it before you publish.

## Publishing it

The disk images are not committed (`dist/` is ignored), so a host that builds from git
never sees them. There are two ways round that.

### Option 1: Vercel, from your Mac (simplest)

The disk images go up with the site, straight from your machine:

```
./build-macos-all.sh --dmg                        # builds both .dmg files
npm run site:plugins -- --require-downloads       # builds the site around them
npx vercel deploy plugins-site/public --prod      # uploads it
```

The first time, Vercel asks you to log in and name the project. It prints the address when it
has finished. To publish a new version, run the same three lines again.

### Option 2: a release, then Render or Vercel from git

If this repository is **public**, GitHub can build and host the disk image for you. Push a
tag and `.github/workflows/macos.yml` builds both plugins on a Mac runner and attaches
`Fracture-and-Crate-<version>-macOS.dmg`, one image holding both, to a release:

```
git tag v0.1.0 && git push origin v0.1.0
```

Point both downloads in `site.json` at it:

```
"downloads": {
  "fracture": { "macOS": "https://github.com/YOU/REPO/releases/download/v0.1.0/Fracture-and-Crate-0.1.0-macOS.dmg" },
  "crate":    { "macOS": "https://github.com/YOU/REPO/releases/download/v0.1.0/Fracture-and-Crate-0.1.0-macOS.dmg" }
}
```

Release links from a private repository do not open for the public. In that case, put the
`.dmg` files somewhere public (Dropbox, or Google Drive with a direct link) and use those
links instead.

Then connect the host to this repository:

**Vercel**: Add New, then Project, then import the repository, and set **Root Directory** to
`plugins-site`. `plugins-site/vercel.json` sets the build command, the output folder and the
headers.

**Render**: New, then Static Site, then the repository.

- Build command: `node plugins-site/build.mjs`
- Publish directory: `plugins-site/public`
- Leave Root Directory empty.
- Under Headers, add each header from `headers.mjs` for the path `/*`.

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
