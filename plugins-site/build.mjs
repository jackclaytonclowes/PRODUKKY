/**
 * Builds the plugins' download site into plugins-site/public: a static folder
 * any host can serve (Render, Vercel, or anything else that serves files).
 *
 *   node plugins-site/build.mjs                  build, linking whatever downloads exist
 *   node plugins-site/build.mjs --require-downloads   fail unless every download is there
 *
 * No dependencies beyond Node itself, so a host's build step needs no install.
 *
 * What goes in:
 *   src/                    the page, the guide template, the stylesheet
 *   img/                    screenshots of both panels (rendered by host_smoke)
 *   ../plugin/GUIDE.md      the guides the plugins show behind their Guide
 *   ../crate/GUIDE.md       button, rendered here as pages: one source, so the
 *                           site and the plugin cannot say different things
 *   ../fx/fracture.html     the browser version, served as-is under play/
 *   site.json               the title, who made it, a contact, and download links
 *
 * Downloads, for each plugin and each system (macOS, Windows), in this order:
 *   1. a URL in site.json ("downloads": {"crate": {"macOS": "https://...",
 *      "Windows": "https://..."}}), for files hosted somewhere else
 *   2. the newest <NAME>-<version>-macOS.dmg, or <NAME>- or Fracture-and-Crate-
 *      <version>-Windows-Setup.exe, in plugins-site/downloads/, or where the build
 *      scripts leave them (plugin/dist, crate/dist, dist). It is copied into
 *      public/downloads/, and its size and SHA-256 are printed on the page
 *   3. otherwise the button says "not built yet", and the build says so
 *
 * The response headers are in headers.mjs, and written out as public/vercel.json
 * so a prebuilt folder deploys with them.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { headers } from './headers.mjs';
import { Canvas, hex } from '../scripts/lib/png.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const out = path.join(here, 'public');
const requireDownloads = process.argv.includes('--require-downloads');

// the test suite points these elsewhere, so it can build with a stand-in
// download without touching the real folders
const configPath = process.env.PLUGINS_SITE_CONFIG || path.join(here, 'site.json');
const config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
const downloadDirs = process.env.PLUGINS_SITE_DOWNLOADS
  ? [process.env.PLUGINS_SITE_DOWNLOADS]
  : [path.join(here, 'downloads')];
const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const plugins = [
  { id: 'fracture', name: 'FRACTURE', dir: 'plugin' },
  { id: 'crate',    name: 'CRATE',    dir: 'crate' },
];
for (const p of plugins) {
  const cm = fs.readFileSync(path.join(root, p.dir, 'CMakeLists.txt'), 'utf8');
  const m = cm.match(new RegExp(`project\\(${p.name} VERSION ([0-9.]+)`));
  if (!m) throw new Error(`no version in ${p.dir}/CMakeLists.txt`);
  p.version = m[1];
}

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(path.join(out, 'img'), { recursive: true });
fs.mkdirSync(path.join(out, 'play'), { recursive: true });

// ------------------------------------------------------------------ downloads
// One button per system. The Mac build is a disk image per plugin, or the one
// CI makes holding both; the Windows build is an installer, normally the one
// packaging/windows makes holding both (Fracture-and-Crate-<version>-Windows-Setup.exe).
const platforms = [
  { key: 'macOS', label: 'Mac', ext: '.dmg',
    pattern: p => new RegExp(`^${p.name}-([0-9.]+)-macOS\\.dmg$`),
    what: 'Audio Unit, VST3 and standalone app', thing: 'disk image',
    check: f => `In Terminal, <code>shasum -a 256 ~/Downloads/${esc(f)}</code> should print:`,
    build: './build-macos-all.sh --dmg' },
  { key: 'Windows', label: 'Windows', ext: '.exe',
    pattern: p => new RegExp(`^(?:${p.name}|Fracture-and-Crate)-([0-9.]+)-Windows-Setup\\.exe$`),
    what: 'VST3 and standalone app for 64-bit Windows', thing: 'installer',
    check: f => `In PowerShell, <code>Get-FileHash $HOME\\Downloads\\${esc(f)}</code> should print (in capitals):`,
    build: '.\\build-windows.ps1 -Package' },
];
const versionKey = v => v.split('.').map(n => n.padStart(6, '0')).join('.');
function findLocal(p, platform) {
  const pattern = platform.pattern(p);
  const found = [];
  const dirs = process.env.PLUGINS_SITE_DOWNLOADS ? downloadDirs
    : [...downloadDirs, path.join(root, p.dir, 'dist'), path.join(root, 'dist')];
  for (const d of dirs) {
    if (!fs.existsSync(d)) continue;
    for (const f of fs.readdirSync(d)) {
      const m = f.match(pattern);
      if (m) found.push({ file: path.join(d, f), name: f, version: m[1] });
    }
  }
  found.sort((a, b) => versionKey(b.version).localeCompare(versionKey(a.version)));
  return found[0];
}
const mb = n => (n / 1048576).toFixed(1) + ' MB';

const summary = [];
let missing = 0;
function platformBlock(p, platform) {
  const url = config.downloads?.[p.id]?.[platform.key];
  const button = `Download for ${platform.label}`;
  if (url) {
    if (!/^https:\/\//.test(url)) throw new Error(`site.json: the ${p.id} ${platform.key} download must be an https:// URL`);
    summary.push(`${p.name} (${platform.label}): linked to ${url}`);
    // the release CI makes is one file holding both plugins; say so, so
    // nobody downloads it twice
    const shared = plugins.every(q => config.downloads?.[q.id]?.[platform.key] === url);
    return `<a class="btn" href="${esc(url)}" data-os="${platform.key}">${button} <small>${platform.ext}</small></a>
            <p class="meta">${shared
              ? `One ${platform.thing} with both FRACTURE and CRATE: ${platform.what}.`
              : `${platform.what[0].toUpperCase() + platform.what.slice(1)} in one ${platform.thing}.`}</p>`;
  }
  const local = findLocal(p, platform);
  if (local) {
    fs.mkdirSync(path.join(out, 'downloads'), { recursive: true });
    fs.copyFileSync(local.file, path.join(out, 'downloads', local.name));
    const bytes = fs.readFileSync(local.file);
    const sha = crypto.createHash('sha256').update(bytes).digest('hex');
    if (local.version !== p.version)
      console.warn(`  ! ${local.name} is version ${local.version}, the source says ${p.version}`);
    summary.push(`${p.name} (${platform.label}): ${path.relative(root, local.file)} (${mb(bytes.length)})`);
    return `<a class="btn" href="downloads/${esc(local.name)}" data-os="${platform.key}" download>${button} <small>${esc(mb(bytes.length))}</small></a>
            <p class="meta">${esc(local.name)}: ${platform.what} in one ${platform.thing}.</p>
            <details><summary>Check the download</summary>
              <p>${platform.check(local.name)}</p>
              <p class="sum">${sha}</p>
            </details>`;
  }
  missing++;
  summary.push(`${p.name} (${platform.label}): NO DOWNLOAD (build it with ${platform.build}, or set a URL in site.json)`);
  return `<span class="btn off" data-os="${platform.key}">${platform.label} download coming soon</span>
          <p class="meta">This build has not been published yet.</p>`;
}
const downloadBlock = p => platforms.map(platform => platformBlock(p, platform)).join('\n            ');

// ------------------------------------------------------------------ the guides
// The same small subset of Markdown the plugins draw (Source/Guide.h):
// # title, ## sections, paragraphs, "- " bullets and **bold**.
function inline(s) {
  return esc(s).replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
}
function renderGuide(md) {
  const lines = md.replace(/\r/g, '').split('\n');
  let title = '', intro = [], sections = [], cur = null, list = null;
  const target = () => (cur ? cur.body : intro);
  const flush = () => { if (list) { target().push(`<ul>${list.join('')}</ul>`); list = null; } };
  for (const line of lines) {
    if (line.startsWith('# ')) { title = line.slice(2).trim(); continue; }
    if (line.startsWith('## ')) {
      flush();
      cur = { heading: line.slice(3).trim(), body: [] };
      sections.push(cur);
      continue;
    }
    if (line.startsWith('- ')) { (list ??= []).push(`<li>${inline(line.slice(2))}</li>`); continue; }
    flush();
    if (line.trim()) target().push(`<p>${inline(line.trim())}</p>`);
  }
  flush();
  const slug = s => s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
  return {
    title,
    intro: intro.join('\n'),
    toc: sections.map(s => `<li><a href="#${slug(s.heading)}">${esc(s.heading)}</a></li>`).join(''),
    sections: sections.map(s =>
      `<section id="${slug(s.heading)}"><h2>${esc(s.heading)}</h2><div class="in">${s.body.join('\n')}</div></section>`
    ).join('\n'),
  };
}

// ------------------------------------------------------------------ assemble
// the three marks, as a data URI: a linked icon is one more request per visit
const favicon = 'data:image/svg+xml,' + encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><rect width="32" height="32" fill="#d6d1c6"/>' +
  '<rect x="3" y="9" width="8" height="14" fill="#17150f"/><circle cx="16" cy="16" r="4.5" fill="#c0392f"/>' +
  '<path d="M21 23V13a8 8 0 0 1 8 8v2z" fill="#1e4b8f"/></svg>');

const common = {
  title: esc(config.title || 'Fracture & Crate'),
  strap: esc(config.strap || 'Audio plugins'),
  maker: config.maker ? ` · made by ${esc(config.maker)}` : '',
  favicon,
  contact: config.contact
    ? `<p>Send it to <a href="mailto:${esc(config.contact)}">${esc(config.contact)}</a>.</p>`
    : '<p>Send it to whoever gave you the link.</p>',
};
for (const p of plugins) common[`version:${p.id}`] = esc(p.version);

function fill(template, values, name) {
  const html = template.replace(/\{\{([a-z:]+)\}\}/g, (all, key) => {
    if (!(key in values)) throw new Error(`${name}: nothing to put in {{${key}}}`);
    return values[key];
  });
  return html;
}

const index = fs.readFileSync(path.join(here, 'src', 'index.html'), 'utf8');
const values = { ...common };
for (const p of plugins) values[`download:${p.id}`] = downloadBlock(p);
fs.writeFileSync(path.join(out, 'index.html'), fill(index, values, 'index.html'));

const guideTemplate = fs.readFileSync(path.join(here, 'src', 'guide.html'), 'utf8');
for (const p of plugins) {
  const g = renderGuide(fs.readFileSync(path.join(root, p.dir, 'GUIDE.md'), 'utf8'));
  fs.writeFileSync(path.join(out, `guide-${p.id}.html`), fill(guideTemplate, {
    ...common, name: esc(p.name), id: p.id, version: esc(p.version),
    heading: esc(g.title), intro: g.intro, toc: g.toc, sections: g.sections,
  }, `guide-${p.id}.html`));
}

fs.copyFileSync(path.join(here, 'src', 'site.css'), path.join(out, 'site.css'));
for (const f of fs.readdirSync(path.join(here, 'img')))
  fs.copyFileSync(path.join(here, 'img', f), path.join(out, 'img', f));
fs.copyFileSync(path.join(root, 'fx', 'fracture.html'), path.join(out, 'play', 'fracture.html'));
// The browser version has no icon link of its own, so a browser asks for
// /favicon.ico; answering it keeps the console clean. The three marks at 32 px,
// drawn by the repository's own PNG writer and wrapped in an ICO container.
{
  const c = new Canvas(32);
  c.roundRect(0, 0, 32, 32, 0, hex('#d6d1c6'));
  c.roundRect(2, 9, 8, 14, 0, hex('#17150f'));
  c.roundRect(12, 12, 9, 9, 4.5, hex('#c0392f'));
  c.roundRect(23, 13, 8, 10, 3, hex('#1e4b8f'));
  const png = c.toPNG();
  const ico = Buffer.alloc(22);
  ico.writeUInt16LE(1, 2);                 // type: icon
  ico.writeUInt16LE(1, 4);                 // one image
  ico.writeUInt8(32, 6); ico.writeUInt8(32, 7);
  ico.writeUInt16LE(1, 10);                // colour planes
  ico.writeUInt16LE(32, 12);               // bits per pixel
  ico.writeUInt32LE(png.length, 14);
  ico.writeUInt32LE(22, 18);               // the PNG follows the header
  fs.writeFileSync(path.join(out, 'favicon.ico'), Buffer.concat([ico, png]));
}
fs.writeFileSync(path.join(out, 'vercel.json'), JSON.stringify({ headers }, null, 2) + '\n');

console.log(`built ${path.relative(root, out)}/`);
for (const line of summary) console.log('  ' + line);
if (missing && requireDownloads) {
  console.error(`\n${missing} download(s) missing, and --require-downloads was given.`);
  process.exit(1);
}
