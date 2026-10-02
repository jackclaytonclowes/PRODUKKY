/**
 * The plugins' download site (plugins-site/), built and served over local HTTP
 * with the same headers a deployment sends, then read in a real browser.
 *
 *   npm run test:plugins-site
 *
 * Three builds: one with a local disk image and Windows installer (FRACTURE)
 * and hosted URLs (CRATE), so both kinds of download are checked end to end
 * on both systems; one with nothing,
 * which must say "coming soon" rather than link to a file that is not there;
 * and --require-downloads on that one, which must refuse.
 *
 * Screenshots land in tests/screenshots/plugins-site-*.png. Look at them:
 * layout does not fail assertions.
 */
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import crypto from 'node:crypto';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer';
import { headers, renderYaml } from '../plugins-site/headers.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SITE = path.join(root, 'plugins-site');
const PUBLIC = path.join(SITE, 'public');
const SHOTS = path.join(root, 'tests', 'screenshots');
const headful = process.argv.includes('--head');

let passed = 0;
const failures = [];
function check(name, ok, detail = '') {
  if (ok) { passed++; console.log(`  ok    ${name}`); }
  else { failures.push(`${name}${detail ? ' — ' + detail : ''}`); console.log(`  FAIL  ${name}${detail ? ' — ' + detail : ''}`); }
}

const version = dir => fs.readFileSync(path.join(root, dir, 'CMakeLists.txt'), 'utf8')
  .match(/project\(\w+ VERSION ([0-9.]+)/)[1];
const build = (env, args = []) => spawnSync(process.execPath, [path.join(SITE, 'build.mjs'), ...args],
  { env: { ...process.env, ...env }, encoding: 'utf8' });

const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.png': 'image/png',
                '.json': 'application/json', '.dmg': 'application/x-apple-diskimage',
                '.ico': 'image/x-icon' };
const headerObject = Object.fromEntries(headers[0].headers.map(h => [h.key, h.value]));

async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'plugins-site-'));
  const fv = version('plugin');
  const dmgName = `FRACTURE-${fv}-macOS.dmg`;
  const dmgBytes = crypto.randomBytes(300000);                    // a stand-in disk image
  const dmgDir = path.join(tmp, 'downloads');
  fs.mkdirSync(dmgDir);
  fs.writeFileSync(path.join(dmgDir, dmgName), dmgBytes);
  fs.writeFileSync(path.join(dmgDir, `FRACTURE-0.0.1-macOS.dmg`), 'an older build');
  // the Windows installer packaging/windows makes holds both plugins
  const exeName = `Fracture-and-Crate-${fv}-Windows-Setup.exe`;
  const exeBytes = crypto.randomBytes(200000);
  fs.writeFileSync(path.join(dmgDir, exeName), exeBytes);
  fs.writeFileSync(path.join(dmgDir, 'Fracture-and-Crate-0.0.1-Windows-Setup.exe'), 'an older build');
  const crateUrl = 'https://downloads.example.org/CRATE-macOS.dmg';
  const crateWinUrl = 'https://downloads.example.org/CRATE-Windows-Setup.exe';
  const config = path.join(tmp, 'site.json');
  fs.writeFileSync(config, JSON.stringify({
    title: 'Fracture & Crate', strap: 'Audio plugins, in beta', maker: 'Test Maker',
    contact: 'someone@example.org', downloads: { crate: { macOS: crateUrl, Windows: crateWinUrl } },
  }));

  console.log('\nBuild');
  {
    const empty = path.join(tmp, 'none');
    fs.mkdirSync(empty);
    const bare = path.join(tmp, 'bare.json');
    fs.writeFileSync(bare, JSON.stringify({ title: 'Fracture & Crate', downloads: {} }));
    const r = build({ PLUGINS_SITE_DOWNLOADS: empty, PLUGINS_SITE_CONFIG: bare }, ['--require-downloads']);
    check('with nothing to download, --require-downloads refuses', r.status === 1 && /missing/.test(r.stderr),
          `exit ${r.status}`);
    const html = fs.readFileSync(path.join(PUBLIC, 'index.html'), 'utf8');
    check('  ... and the page says coming soon instead of linking to nothing',
          (html.match(/Mac download coming soon/g) || []).length === 2
            && (html.match(/Windows download coming soon/g) || []).length === 2 && !/href="downloads\//.test(html));
  }
  {
    // the committed site.json, as a host builds it: no disk image on the machine
    const empty = path.join(tmp, 'none');
    const real = JSON.parse(fs.readFileSync(path.join(SITE, 'site.json'), 'utf8'));
    const r = build({ PLUGINS_SITE_DOWNLOADS: empty });
    const html = fs.readFileSync(path.join(PUBLIC, 'index.html'), 'utf8');
    const urls = ['fracture', 'crate'].map(id => real.downloads?.[id]?.macOS);
    const hrefs = [...html.matchAll(/class="btn" href="(https:[^"]+)" data-os="macOS"/g)].map(m => m[1]);
    check('the committed site.json links both plugins to a hosted disk image',
          r.status === 0 && urls.every(u => /^https:\/\//.test(u)) && hrefs.length === 2
            && hrefs.every((h, i) => h === urls[i]), hrefs.join(' '));
    check('  ... and when both share one image, the page says it holds both',
          urls[0] !== urls[1] || (html.match(/One disk image with both FRACTURE and CRATE/g) || []).length === 2);
  }
  check('render.yaml is the Blueprint headers.mjs describes',
        fs.readFileSync(path.join(root, 'render.yaml'), 'utf8') === renderYaml());
  const r = build({ PLUGINS_SITE_DOWNLOADS: dmgDir, PLUGINS_SITE_CONFIG: config });
  check('builds with a local disk image and installer, and hosted ones', r.status === 0, r.stderr);
  check('  ... and copies only the newest disk image and installer',
        fs.readdirSync(path.join(PUBLIC, 'downloads')).sort().join() === [dmgName, exeName].sort().join(),
        fs.readdirSync(path.join(PUBLIC, 'downloads')).join());
  const committed = JSON.parse(fs.readFileSync(path.join(SITE, 'vercel.json'), 'utf8'));
  const emitted = JSON.parse(fs.readFileSync(path.join(PUBLIC, 'vercel.json'), 'utf8'));
  check('the committed vercel.json carries the same headers as headers.mjs',
        JSON.stringify(committed.headers) === JSON.stringify(headers)
          && committed.outputDirectory === 'public' && /build\.mjs/.test(committed.buildCommand));
  check('  ... and so does the one written into the built folder',
        JSON.stringify(emitted) === JSON.stringify({ headers }));
  const sources = ['index.html', 'guide-crate.html', 'guide-fracture.html', 'site.css']
    .map(f => fs.readFileSync(path.join(PUBLIC, f), 'utf8'));
  check('no page or stylesheet loads anything from elsewhere',
        sources.every(s => !/(src|href)="(https?:)?\/\//.test(s.replace(/href="https:\/\/downloads\.example\.org[^"]*"/g, ''))
                          && !/@import|url\(\s*['"]?https?:/.test(s)));
  check('the pages run no scripts', sources.slice(0, 3).every(s => !/<script/i.test(s)));

  // ---- serve it, as a host would
  const requests = [];
  const server = http.createServer((req, res) => {
    requests.push(req.url);
    let rel = decodeURIComponent(req.url.split('?')[0]);
    if (rel.endsWith('/')) rel += 'index.html';
    const file = path.join(PUBLIC, rel);
    if (!file.startsWith(PUBLIC) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      res.writeHead(404).end('not found'); return;
    }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream', ...headerObject });
    res.end(fs.readFileSync(file));
  });
  await new Promise(ok => server.listen(0, '127.0.0.1', ok));
  const origin = `http://127.0.0.1:${server.address().port}`;

  const browser = await puppeteer.launch({
    headless: headful ? false : 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--autoplay-policy=no-user-gesture-required',
           '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream'],
  });
  const problems = [];
  const offsite = [];
  const newPage = async () => {
    const page = await browser.newPage();
    page.on('pageerror', e => problems.push(`${page.url()}: ${e.message}`));
    page.on('console', m => { if (m.type() === 'error') problems.push(`${page.url()}: console: ${m.text()}`); });
    await page.evaluateOnNewDocument(() => {
      document.addEventListener('securitypolicyviolation', e =>
        console.error(`CSP blocked ${e.blockedURI} (${e.violatedDirective})`));
    });
    await page.setRequestInterception(true);
    page.on('request', q => {
      const u = q.url();
      if (u.startsWith(origin) || u.startsWith('data:') || u.startsWith('blob:')) q.continue();
      else { offsite.push(u); q.abort(); }
    });
    return page;
  };

  console.log('\nThe page');
  const page = await newPage();
  await page.setViewport({ width: 1280, height: 900 });
  await page.goto(origin + '/', { waitUntil: 'networkidle0' });
  const info = await page.evaluate(() => ({
    title: document.title,
    h1: document.querySelector('h1')?.textContent,
    images: [...document.images].map(i => ({ src: i.getAttribute('src'), w: i.naturalWidth, alt: i.alt })),
    downloads: [...document.querySelectorAll('.get a.btn')].map(a => ({ href: a.getAttribute('href'), dl: a.hasAttribute('download'),
                                                                     os: a.dataset.os, text: a.textContent })),
    sums: [...document.querySelectorAll('.sum')].map(s => s.textContent.trim()),
    links: [...document.querySelectorAll('a[href]')].map(a => a.getAttribute('href')),
    ids: [...document.querySelectorAll('[id]')].map(e => e.id),
    footer: document.querySelector('footer').textContent,
    mail: document.querySelector('a[href^="mailto:"]')?.getAttribute('href'),
    versions: [...document.querySelectorAll('.panel > h2 small')].map(s => s.textContent),
  }));
  check('the page loads with its title', info.title === 'Fracture & Crate' && /Two plugins/.test(info.h1));
  await page.evaluate(async () => { for (const i of document.images) { i.loading = 'eager'; await i.decode().catch(() => {}); } });
  const widths = await page.evaluate(() => [...document.images].map(i => i.naturalWidth));
  check('all three screenshots load, each with a description', widths.length === 3 && widths.every(w => w > 1000)
        && info.images.every(i => i.alt.length > 40), JSON.stringify(widths));
  const local = info.downloads.find(d => d.href === `downloads/${dmgName}`);
  check('FRACTURE links to its disk image, as a download', local && local.dl, JSON.stringify(info.downloads));
  check('CRATE links to where its disk image is hosted', info.downloads.some(d => d.href === crateUrl));
  const exe = info.downloads.find(d => d.href === `downloads/${exeName}`);
  check('FRACTURE links to the Windows installer, as a download, on a Windows button',
        exe && exe.dl && exe.os === 'Windows' && /Download for Windows/.test(exe.text), JSON.stringify(info.downloads));
  check('CRATE links to where its Windows installer is hosted',
        info.downloads.some(d => d.href === crateWinUrl && d.os === 'Windows'));
  check('each plugin has one Mac and one Windows button',
        info.downloads.length === 4 && info.downloads.filter(d => d.os === 'Windows').length === 2);
  const got = await page.evaluate(async h => {
    const res = await fetch(h);
    const buf = new Uint8Array(await res.arrayBuffer());
    const sha = [...new Uint8Array(await crypto.subtle.digest('SHA-256', buf))].map(b => b.toString(16).padStart(2, '0')).join('');
    return { status: res.status, size: buf.length, sha };
  }, `downloads/${dmgName}`);
  check('the disk image downloads whole', got.status === 200 && got.size === dmgBytes.length, JSON.stringify(got));
  check('  ... and the checksum printed on the page is the file\'s',
        info.sums.length === 2 && info.sums[0] === crypto.createHash('sha256').update(dmgBytes).digest('hex')
          && got.sha === info.sums[0]);
  const gotExe = await page.evaluate(async h => {
    const res = await fetch(h);
    const buf = new Uint8Array(await res.arrayBuffer());
    const sha = [...new Uint8Array(await crypto.subtle.digest('SHA-256', buf))].map(b => b.toString(16).padStart(2, '0')).join('');
    return { status: res.status, size: buf.length, sha };
  }, `downloads/${exeName}`);
  check('the Windows installer downloads whole, and its printed checksum is the file\'s',
        gotExe.status === 200 && gotExe.size === exeBytes.length && gotExe.sha === info.sums[1]
          && info.sums[1] === crypto.createHash('sha256').update(exeBytes).digest('hex'), JSON.stringify(gotExe));
  check('versions come from the CMake projects',
        info.versions.join() === `v${fv},v${version('crate')}`, info.versions.join());
  check('the maker and the contact from site.json appear', /Test Maker/.test(info.footer)
        && info.mail === 'mailto:someone@example.org');
  const internal = [...new Set(info.links.filter(h => !/^(https?:|mailto:)/.test(h)))];
  const broken = [];
  for (const h of internal) {
    const [file, hash] = h.split('#');
    if (file && file !== './') {
      const status = await page.evaluate(async u => (await fetch(u, { method: 'HEAD' })).status, file);
      if (status !== 200) broken.push(`${h} ${status}`);
    }
    if (hash && (!file || file === './') && !info.ids.includes(hash)) broken.push(`${h} (no such section)`);
  }
  check('every link on the page goes somewhere', broken.length === 0 && internal.length >= 8, broken.join(', '));
  await page.screenshot({ path: path.join(SHOTS, 'plugins-site-desktop.png'), fullPage: true });

  await page.setViewport({ width: 375, height: 800, isMobile: true, hasTouch: true, deviceScaleFactor: 2 });
  await page.goto(origin + '/', { waitUntil: 'networkidle0' });
  const phone = await page.evaluate(() => ({
    scroll: document.documentElement.scrollWidth, view: window.innerWidth,
    gutter: document.querySelector('.hero h1').getBoundingClientRect().left,
  }));
  // against the device width, not innerWidth: on a mobile viewport the browser
  // widens innerWidth to fit whatever overflows, so that comparison always passes
  check('on a phone, nothing scrolls sideways', phone.scroll <= 375 && phone.view === 375, JSON.stringify(phone));
  check('  ... and text keeps a 16px gutter', Math.round(phone.gutter) === 16, JSON.stringify(phone));
  await page.screenshot({ path: path.join(SHOTS, 'plugins-site-phone.png'), fullPage: true });
  await page.close();

  console.log('\nThe guides');
  for (const [id, dir] of [['fracture', 'plugin'], ['crate', 'crate']]) {
    const md = fs.readFileSync(path.join(root, dir, 'GUIDE.md'), 'utf8');
    const heads = md.split('\n').filter(l => l.startsWith('## ')).map(l => l.slice(3).trim());
    const bullets = md.split('\n').filter(l => l.startsWith('- ')).length;
    const bold = (md.match(/\*\*(.+?)\*\*/g) || []).length;
    const g = await newPage();
    await g.setViewport({ width: 1100, height: 900 });
    await g.goto(`${origin}/guide-${id}.html`, { waitUntil: 'networkidle0' });
    const s = await g.evaluate(() => ({
      h1: document.querySelector('h1').textContent,
      heads: [...document.querySelectorAll('section h2')].map(h => h.textContent),
      toc: [...document.querySelectorAll('.toc a')].map(a => a.getAttribute('href').slice(1)),
      ids: [...document.querySelectorAll('section[id]')].map(e => e.id),
      bullets: document.querySelectorAll('main li:not(.toc li)').length,
      bold: document.querySelectorAll('main strong').length,
      stars: document.querySelector('main').textContent.includes('**'),
      footer: document.querySelector('footer').textContent,
      back: document.querySelector('nav a').getAttribute('href'),
    }));
    check(`${id}: every section of GUIDE.md is on the page, in order`,
          s.h1 === md.split('\n')[0].slice(2) && JSON.stringify(s.heads) === JSON.stringify(heads),
          `${s.heads.length} of ${heads.length}`);
    check(`${id}:   ... with every bullet and every bold word`,
          s.bullets === bullets && s.bold === bold && !s.stars, `${s.bullets}/${bullets} bullets, ${s.bold}/${bold} bold`);
    check(`${id}:   ... a contents list that reaches each one, and its own version`,
          JSON.stringify(s.toc) === JSON.stringify(s.ids) && s.footer.includes(`${id.toUpperCase()} ${version(dir)}`)
            && s.back === `./#${id}`);
    if (id === 'fracture') await g.screenshot({ path: path.join(SHOTS, 'plugins-site-guide.png') });
    await g.close();
  }

  console.log('\nThe browser version');
  {
    const b = await newPage();
    await b.setViewport({ width: 1280, height: 900 });
    await b.goto(`${origin}/play/fracture.html`, { waitUntil: 'networkidle0' });
    await b.select('#srcSel', 'pluck');
    await b.click('#btnPlay');
    await new Promise(ok => setTimeout(ok, 800));
    const live = await b.evaluate(() => ({
      playing: document.getElementById('btnPlay').textContent.includes('Stop'),
      meter: document.querySelectorAll('.meter .num')[1]?.textContent,
    }));
    check('plays under the site\'s headers', live.playing && live.meter && !/-inf|−∞/.test(live.meter),
          JSON.stringify(live));
    const mic = await b.evaluate(async () => {
      try { const s = await navigator.mediaDevices.getUserMedia({ audio: true }); s.getTracks().forEach(t => t.stop()); return 'ok'; }
      catch (e) { return e.name; }
    });
    check('  ... and the headers still let it ask for the microphone', mic === 'ok', mic);
    await b.close();
  }

  console.log('\nSafety');
  check('nothing on the site asks for anything from elsewhere', offsite.length === 0, offsite.join(', '));
  check('no script errors and no blocked resources, on any page', problems.length === 0, problems.join(' | '));
  const ico = fs.readFileSync(path.join(PUBLIC, 'favicon.ico'));
  check('the icon a browser asks the browser version for is a real ICO',
        ico.readUInt16LE(2) === 1 && ico.readUInt16LE(4) === 1
          && ico.subarray(22, 26).toString('latin1') === '\x89PNG');

  await browser.close();
  server.close();
  fs.rmSync(tmp, { recursive: true, force: true });
  // leave public/ as a plain build, without the stand-in disk image
  execFileSync(process.execPath, [path.join(SITE, 'build.mjs')], { stdio: 'ignore' });

  console.log(`\n${passed} assertions passed, ${failures.length} failed`);
  for (const f of failures) console.log('  - ' + f);
  console.log(`screenshots: ${path.relative(root, SHOTS)}/plugins-site-*.png`);
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
