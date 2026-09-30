/**
 * Smoke test for fx/fracture.html, the multi-FX distortion unit.
 *
 * A distortion box has two failure modes that matter and neither shows up by
 * reading the code: a shaper or a feedback loop that produces NaN/Inf (which
 * silently kills the whole graph for the rest of the session), and a chain
 * that runs away in level. So the test builds the real graph in an
 * OfflineAudioContext, renders noise through it, and asserts the output is
 * finite, audible and bounded — for every distortion mode, at full drive, and
 * with the feedback loop pushed hard.
 *
 *   npm run test:fx
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const APP = path.join(root, 'fx', 'fracture.html');
const SHOTS = path.join(root, 'tests', 'screenshots');
const headful = process.argv.includes('--head');

let passed = 0;
const failures = [];
function check(name, ok, detail) {
  if (ok) { passed++; console.log(`  ok    ${name}`); }
  else { failures.push(`${name}${detail ? ' — ' + detail : ''}`); console.log(`  FAIL  ${name}${detail ? ' — ' + detail : ''}`); }
}
const wait = ms => new Promise(r => setTimeout(r, ms));

/* Rendered in the page: build the engine in an OfflineAudioContext, push
   noise through it, and report what came out. */
const RENDER = `async (patch, seconds) => {
  const sr = 44100;
  const ctx = new OfflineAudioContext(2, Math.round(sr*seconds), sr);
  const eng = window.FX.createEngine(ctx);
  await eng.initWorklet();
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  const len = Math.round(sr*seconds);
  const buf = ctx.createBuffer(2, len, sr);
  for (let c = 0; c < 2; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < len; i++) d[i] = (Math.random()*2-1)*0.5;
  }
  const s = ctx.createBufferSource();
  s.buffer = buf; s.connect(eng.input); s.start();
  const r = await ctx.startRendering();
  let peak = 0, bad = 0, sum = 0, n = 0;
  for (let c = 0; c < r.numberOfChannels; c++) {
    const d = r.getChannelData(c);
    // skip the first 30ms: the parameter ramps are still settling
    for (let i = Math.round(sr*0.03); i < d.length; i++) {
      const v = d[i];
      if (!Number.isFinite(v)) { bad++; continue; }
      const a = Math.abs(v);
      if (a > peak) peak = a;
      sum += v*v; n++;
    }
  }
  return { peak, bad, rms: Math.sqrt(sum/Math.max(1,n)), crush: eng.crushAvailable };
}`;

/* The same graph, but reporting a per-channel envelope rather than one number:
   a tremolo is only doing its job if the level moves, and only auto-panning if
   the two channels move in opposite directions. */
const ENVELOPE = `async (patch, seconds) => {
  const sr = 44100;
  const ctx = new OfflineAudioContext(2, Math.round(sr*seconds), sr);
  const eng = window.FX.createEngine(ctx);
  await eng.initWorklet();
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  const len = Math.round(sr*seconds);
  const buf = ctx.createBuffer(2, len, sr);
  for (let c = 0; c < 2; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < len; i++) d[i] = (Math.random()*2-1)*0.5;
  }
  const s = ctx.createBufferSource();
  s.buffer = buf; s.connect(eng.input); s.start();
  const r = await ctx.startRendering();
  const win = Math.round(sr*0.005), skip = Math.round(sr*0.08);
  const env = [[], []];
  for (let c = 0; c < 2; c++) {
    const d = r.getChannelData(c);
    for (let i = skip; i + win < d.length; i += win) {
      let sum = 0;
      for (let j = 0; j < win; j++) sum += d[i+j]*d[i+j];
      env[c].push(Math.sqrt(sum/win));
    }
  }
  return { L: env[0], R: env[1], worklet: eng.crushAvailable };
}`;

/* A sine or a quiet noise through the whole engine, returning the samples, for
   the anti-aliasing and the delay checks. `fallback` builds the old
   WaveShaperNode alone instead, to compare against. */
const SIGNAL = `async (patch, kind, freq, amp, seconds, fallback) => {
  const sr = 48000, len = Math.round(sr*seconds);
  const ctx = new OfflineAudioContext(2, len, sr);
  const buf = ctx.createBuffer(2, len, sr);
  let seed = 7;
  const rnd = () => (seed = (seed*16807) % 2147483647)/2147483647*2 - 1;
  for (let c = 0; c < 2; c++){
    const d = buf.getChannelData(c);
    // 'tones' is 24 sines from 200 Hz to 8 kHz at random phases: broadband,
    // but clear of the filters that sit at the edges of the band
    const tones = Array.from({ length:24 }, (_, k) => [200*Math.pow(40, k/23), Math.PI*2*Math.abs(rnd())]);
    for (let i = 0; i < len; i++) d[i] = kind === 'sine' ? amp*Math.sin(2*Math.PI*freq*i/sr)
      : kind === 'tones' ? amp*tones.reduce((acc, [f, ph]) => acc + Math.sin(2*Math.PI*f*i/sr + ph), 0)/24
      : amp*rnd();
  }
  const s = ctx.createBufferSource(); s.buffer = buf;
  let shaperLoaded = false;
  if (fallback){
    const m = window.FX.MODES.find(x => x.id === patch.m0a);
    const D = 40, curve = new Float32Array(32769);
    for (let i = 0; i < curve.length; i++){ const x = i/(curve.length-1)*2 - 1; curve[i] = Math.max(-1, Math.min(1, m.fn(x*D))); }
    const g = ctx.createGain(); g.gain.value = patch.d0a/D;
    const w = ctx.createWaveShaper(); w.curve = curve; w.oversample = '4x';
    s.connect(g); g.connect(w); w.connect(ctx.destination);
  } else {
    const eng = window.FX.createEngine(ctx);
    await eng.initWorklet();
    shaperLoaded = !!eng.shaperAvailable;
    window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
    s.connect(eng.input);
  }
  s.start();
  const r = await ctx.startRendering();
  return { input: Array.from(buf.getChannelData(0)), out: Array.from(r.getChannelData(0)), shaperLoaded };
}`;

// energy that is not a harmonic of f0, against the fundamental, below 18 kHz
function aliasDb(x, f0, sr = 48000){
  const N = 1 << 14, start = x.length - N;
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = x[start + i]*(0.5 - 0.5*Math.cos(2*Math.PI*i/N));
  for (let i = 1, j = 0; i < N; i++){ let bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j){ [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= N; len <<= 1){
    const ang = -2*Math.PI/len;
    for (let i = 0; i < N; i += len) for (let k = 0; k < len/2; k++){
      const wr = Math.cos(ang*k), wi = Math.sin(ang*k);
      const a = i + k, b = a + len/2;
      const tr = re[b]*wr - im[b]*wi, ti = re[b]*wi + im[b]*wr;
      re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
    }
  }
  const bin = sr/N; let fund = 0, alias = 0;
  for (let k = 1; k < N/2 && k*bin < 18000; k++){
    const p = re[k]*re[k] + im[k]*im[k], h = k*bin/f0, off = Math.abs(h - Math.round(h))*f0;
    if (off < 4*bin){ if (Math.round(h) === 1) fund += p; } else alias += p;
  }
  return 10*Math.log10(alias/fund);
}

const stats = a => {
  const mean = a.reduce((x, y) => x + y, 0)/a.length;
  return { mean, min: Math.min(...a), max: Math.max(...a) };
};
const correlation = (a, b) => {
  const ma = stats(a).mean, mb = stats(b).mean;
  let num = 0, da = 0, db = 0;
  for (let i = 0; i < a.length; i++) {
    num += (a[i]-ma)*(b[i]-mb); da += (a[i]-ma)**2; db += (b[i]-mb)**2;
  }
  return num/Math.sqrt(da*db);
};

async function main() {
  if (!fs.existsSync(APP)) { console.error('Missing ' + APP); process.exit(1); }
  fs.mkdirSync(SHOTS, { recursive: true });

  const browser = await puppeteer.launch({
    headless: headful ? false : 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--autoplay-policy=no-user-gesture-required'],
  });
  const pageErrors = [];
  const page = await browser.newPage();
  page.on('pageerror', e => pageErrors.push(e.message));
  page.on('console', m => { if (m.type() === 'error') pageErrors.push('console: ' + m.text()); });
  await page.setViewport({ width: 1280, height: 1500, deviceScaleFactor: 1.2 });
  await page.goto('file://' + APP);
  await wait(300);

  const render = (patch = {}, seconds = 0.3) =>
    page.evaluate(`(${RENDER})(${JSON.stringify(patch)}, ${seconds})`);
  const envelope = (patch = {}, seconds = 0.8) =>
    page.evaluate(`(${ENVELOPE})(${JSON.stringify(patch)}, ${seconds})`);

  console.log('\nPage');
  check('loads without script errors', pageErrors.length === 0, pageErrors.join(' | '));

  const ui = await page.evaluate(() => ({
    knobs: document.querySelectorAll('.knob').length,
    selects: document.querySelectorAll('select').length,
    bands: document.querySelectorAll('.bandpane').length,
    matrix: document.querySelectorAll('.mx').length,
    modes: window.FX.MODES.length,
    params: window.FX.PLIST.length,
  }));
  check('controls rendered', ui.knobs > 40 && ui.selects > 10, JSON.stringify(ui));
  check('three band panes, six matrix slots', ui.bands === 3 && ui.matrix === 6, JSON.stringify(ui));
  check('fourteen distortion modes', ui.modes === 14, String(ui.modes));

  const drawn = await page.evaluate(() => {
    const c = document.getElementById('cvCurve');
    const g = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let lit = 0;
    for (let i = 0; i < g.length; i += 4) if (g[i] > 60 || g[i+1] > 60 || g[i+2] > 60) lit++;
    return lit;
  });
  check('transfer curve is drawn', drawn > 200, 'lit pixels ' + drawn);

  console.log('\nShaper maths');
  const shapers = await page.evaluate(() => {
    const bad = [];
    for (const m of window.FX.MODES) {
      if (Math.abs(m.fn(0)) > 1e-9) bad.push(m.id + ' f(0)!=0');
      for (let x = -40; x <= 40; x += 0.01) {
        const y = m.fn(x);
        if (!Number.isFinite(y) || Math.abs(y) > 1.0000001) { bad.push(m.id + ' unbounded at ' + x.toFixed(2)); break; }
      }
    }
    return bad;
  });
  check('every mode is bounded and passes through zero', shapers.length === 0, shapers.join(', '));
  // F, each mode's antiderivative, is what the anti-aliasing runs on: over any
  // stretch, F(b) - F(a) must be the area under f, jumps and corners included
  const antis = await page.evaluate(() => {
    const bad = []; let seed = 3, checked = 0;
    const rnd = () => (seed = (seed*16807) % 2147483647)/2147483647;
    for (const m of window.FX.MODES){
      if (!m.F) continue;
      checked++;
      let worst = 0;
      for (let k = 0; k < 200; k++){
        const x0 = -40 + 80*rnd(), x1 = x0 + 6*rnd() - 3, n = 20000, h = (x1 - x0)/n;
        let area = 0;
        for (let i = 0; i < n; i++) area += m.fn(x0 + (i + 0.5)*h)*h;
        // the sum itself is out by up to a step at each of Wrap's jumps (at
        // most two in a stretch this long); every other mode is continuous
        const allowed = m.id === 'wrap' ? 1e-5 + 2*Math.abs(h) : 1e-5;
        worst = Math.max(worst, Math.abs((m.F(x1) - m.F(x0)) - area) / allowed * 1e-5);
      }
      if (worst > 1e-5) bad.push(m.id + ' ' + worst.toExponential(1));
    }
    return { bad, checked };
  });
  check('each antiderivative F is the area under its mode (12 modes; Warp and Quantize have none)',
    antis.bad.length === 0 && antis.checked === 12, antis.bad.join(', ') + ` (${antis.checked} checked)`);

  console.log('\nOffline render');
  const base = await render({});
  check('default patch is finite', base.bad === 0, base.bad + ' non-finite samples');
  check('default patch is audible', base.rms > 0.01, 'rms ' + base.rms.toFixed(4));
  check('default patch stays inside full scale', base.peak <= 0.95, 'peak ' + base.peak.toFixed(3));
  check('bit crusher worklet loaded', base.crush === true);

  const modes = await page.evaluate(() => window.FX.MODES.map(m => m.id));
  for (const id of modes) {
    const r = await render({ bands: '1', m0a: id, d0a: 40, sb0: true, m0b: id, d0b: 40 });
    check(`mode ${id} at +32 dB, two stages`,
      r.bad === 0 && r.peak <= 0.95 && r.rms > 0.0005,
      `bad ${r.bad} peak ${r.peak.toFixed(3)} rms ${r.rms.toFixed(4)}`);
  }

  const fb = await render({ bands: '1', m0a: 'hard', d0a: 24, fbAmt: 85, fbTime: 3, fbTone: 14000 }, 0.6);
  check('feedback at 85% does not run away', fb.bad === 0 && fb.peak <= 0.95,
    `bad ${fb.bad} peak ${fb.peak.toFixed(3)}`);

  const crushed = await render({ bands: '1', bits: 2, redux: 32, crMix: 100 });
  check('crush changes the signal', crushed.bad === 0 && Math.abs(crushed.rms - base.rms) > 0.001,
    `rms ${crushed.rms.toFixed(4)} vs ${base.rms.toFixed(4)}`);

  const wrapped = await render({ bands: '1', m0a: 'wrap', d0a: 30, autoGain: false, safety: true });
  check('safety clip contains the worst mode', wrapped.bad === 0 && wrapped.peak <= 0.95,
    'peak ' + wrapped.peak.toFixed(3));

  console.log('\nRouting');
  for (const n of ['1', '2', '3']) {
    const r = await render({ bands: n });
    check(`${n} band(s) audible`, r.bad === 0 && r.rms > 0.01, 'rms ' + r.rms.toFixed(4));
  }
  const muted = await render({ bands: '3', mu0: true, mu1: true, mu2: true, mix: 100 });
  check('muting every band silences the wet path', muted.rms < 0.001, 'rms ' + muted.rms.toFixed(5));
  const soloed = await render({ bands: '3', so1: true });
  check('solo leaves one band audible', soloed.rms > 0.005 && soloed.rms < base.rms,
    `rms ${soloed.rms.toFixed(4)} vs ${base.rms.toFixed(4)}`);
  const dryOnly = await render({ mix: 0 });
  check('dry/wet at 0 passes the input through', dryOnly.bad === 0 && dryOnly.rms > 0.01,
    'rms ' + dryOnly.rms.toFixed(4));

  console.log('\nThe drive shaper (the plugin\'s oversampling and anti-aliasing)');
  {
    const wrapPatch = { bands: '1', m0a: 'wrap', d0a: 25, autoGain: false, safety: false, mix: 100 };
    const now = await page.evaluate(`(${SIGNAL})(${JSON.stringify(wrapPatch)}, 'sine', 3700, 0.5, 0.6, false)`);
    const old = await page.evaluate(`(${SIGNAL})(${JSON.stringify(wrapPatch)}, 'sine', 3700, 0.5, 0.6, true)`);
    check('the drive shaper worklet loaded', now.shaperLoaded === true);
    const aNow = aliasDb(now.out, 3700), aOld = aliasDb(old.out, 3700);
    check('Wrap on a 3.7 kHz tone: aliasing well under the note, and far under the old shaper',
      aNow < -12 && aNow < aOld - 15, `${aNow.toFixed(1)} dB against ${aOld.toFixed(1)} dB with WaveShaperNode`);

    // The dry and wet paths must arrive together, or any mix between them
    // comb-filters. Each is rendered on its own, quietly so the shapers are
    // linear, and the wet one must line up with the dry one to within a sample
    // (the pre-filters' own delay). With the dry paths left undelayed the wet
    // one is 96 samples behind
    const quiet = { autoGain: false, bands: '1', d0a: 1 };
    const take = async patch => (await page.evaluate(`(${SIGNAL})(${JSON.stringify(Object.assign({}, quiet, patch))}, 'tones', 0, 0.002, 0.5, false)`)).out;
    const lineUp = (x, y) => {
      let best = { lag: 0, r: -2 };
      for (let lag = -200; lag <= 200; lag++){
        let g = 0, ex = 0, ey = 0;
        for (let i = 8000; i < x.length - 200; i++){ const a = x[i], b = y[i + lag]; g += a*b; ex += a*a; ey += b*b; }
        const r = g/Math.sqrt(ex*ey);
        if (r > best.r) best = { lag, r };
      }
      return best;
    };
    const dryOut = await take({ mix: 0 });
    for (const [name, patch, against] of [
      ['the main dry path and the wet path', { mix: 100 }, dryOut],
      ['the main dry path and the wet path with stage B on', { mix: 100, sb0: true, d0b: 1 }, dryOut],
      ['a band\'s dry share and its shaped share', { mix: 100 }, await take({ mix: 100, mx0: 0 })],
    ]){
      const lu = lineUp(against, await take(patch));
      check(`${name} arrive together`, Math.abs(lu.lag) <= 1 && lu.r > 0.95, `offset ${lu.lag} samples, correlation ${lu.r.toFixed(3)}`);
    }
  }

  console.log('\nModulation');
  const modded = await render({ bands: '1', d0a: 12, mS0: 'lfo1', mD0: 'd0a', mA0: 100, l1Rate: 8 }, 0.4);
  check('lfo on drive renders finite', modded.bad === 0 && modded.rms > 0.005 && modded.peak <= 0.95,
    `bad ${modded.bad} peak ${modded.peak.toFixed(3)}`);
  const modUi = await page.evaluate(() => {
    const dests = window.FX.PLIST.filter(d => d.mod).map(d => d.id);
    const sel = [...document.querySelectorAll('select')].find(s => s.previousSibling && s.previousSibling.textContent === 'Target');
    return { dests: dests.length, options: sel ? sel.options.length : 0 };
  });
  check('every modulatable parameter is a matrix target', modUi.options === modUi.dests + 1,
    JSON.stringify(modUi));

  console.log('\nLadder and tremolo');
  {
    const base = { bands: '1', mx0: 0, fltType: 'lp', fltFreq: 900, fltQ: 6 };
    const clean = await render({ ...base, fltCirc: 'clean' }, 0.4);
    const analog = await render({ ...base, fltCirc: 'analog' }, 0.4);
    const vintage = await render({ ...base, fltCirc: 'vintage', fltDrive: 6 }, 0.4);
    check('the ladder is in circuit and is not the biquad',
      analog.bad === 0 && analog.rms > 0.002 && Math.abs(analog.rms - clean.rms)/clean.rms > 0.05,
      `clean ${clean.rms.toFixed(4)} analogue ${analog.rms.toFixed(4)}`);
    check('the vintage circuit is different again and still bounded',
      vintage.bad === 0 && vintage.peak <= 0.95 && Math.abs(vintage.rms - analog.rms)/analog.rms > 0.02,
      `analogue ${analog.rms.toFixed(4)} vintage ${vintage.rms.toFixed(4)}`);
    const singing = await render({ ...base, fltCirc: 'analog', fltQ: 18, fltDrive: 12 }, 0.5);
    check('a self-oscillating ladder stays inside the rails',
      singing.bad === 0 && singing.peak <= 0.95, `peak ${singing.peak.toFixed(3)}`);
  }
  {
    const off = await envelope({ bands: '1', mx0: 0, trOn: false });
    const on = await envelope({ bands: '1', mx0: 0, trOn: true, trDiv: 'free', trRate: 6,
                                trDepth: 100, trShape: 100, trEdge: 90 });
    const a = stats(off.L), b = stats(on.L);
    check('the worklet carries the tremolo as well as the crusher', on.worklet === true);
    check('a tremolo at full depth chops the level',
      b.min/b.max < 0.2 && a.min/a.max > 0.4,
      `off ${(a.min/a.max).toFixed(3)} on ${(b.min/b.max).toFixed(3)}`);
    const panned = await envelope({ bands: '1', mx0: 0, trOn: true, trDiv: 'free', trRate: 5,
                                    trDepth: 100, trShape: 0, trSpread: 180, width: 100 });
    check('180 degrees of spread is auto-pan',
      correlation(panned.L, panned.R) < -0.7,
      `correlation ${correlation(panned.L, panned.R).toFixed(3)}`);
    const quarter = await envelope({ bands: '1', mx0: 0, trOn: true, trDiv: '1/4',
                                     trDepth: 100, trShape: 100, trEdge: 90 }, 1.0);
    // 120 BPM is the page default: a quarter is 2 a second, so ~2 dips a second
    let dips = 0;
    const q = stats(quarter.L);
    for (let i = 1; i < quarter.L.length; i++)
      if (quarter.L[i-1] > q.mean && quarter.L[i] <= q.mean) dips++;
    check('a synced tremolo counts the page tempo',
      dips >= 1 && dips <= 3, `${dips} dips in ${(quarter.L.length*0.005).toFixed(2)} s`);
  }

  console.log('\nPresets');
  const presetProblems = await page.evaluate(() => {
    const bad = [];
    for (const pr of window.FX.PRESETS) {
      for (const [k, v] of Object.entries(pr.v)) {
        const d = window.FX.P[k];
        if (!d) { bad.push(`${pr.name}: unknown param ${k}`); continue; }
        if (d.type === 'sel' && !d.opts.some(o => o[0] === v)) bad.push(`${pr.name}: ${k}=${v} not an option`);
        if (d.type === 'knob' && (typeof v !== 'number' || v < d.min || v > d.max)) bad.push(`${pr.name}: ${k}=${v} out of range`);
        if (d.type === 'tgl' && typeof v !== 'boolean') bad.push(`${pr.name}: ${k}=${v} not a boolean`);
      }
    }
    return bad;
  });
  check('every preset value is a real parameter in range', presetProblems.length === 0, presetProblems.join(' | '));

  const presetCount = await page.evaluate(() => window.FX.PRESETS.length);
  for (let i = 0; i < presetCount; i++) {
    const pr = await page.evaluate(i => window.FX.PRESETS[i], i);
    const r = await render(pr.v, 0.35);
    check(`preset "${pr.name}"`, r.bad === 0 && r.peak <= 0.95 && r.rms > 0.002,
      `bad ${r.bad} peak ${r.peak.toFixed(3)} rms ${r.rms.toFixed(4)}`);
  }

  console.log('\nLive context');
  await page.select('#srcSel', 'pluck');
  await page.click('#btnPlay');
  await wait(700);
  const live = await page.evaluate(() => ({
    playing: document.getElementById('btnPlay').textContent.includes('Stop'),
    meter: document.querySelectorAll('.meter .num')[1].textContent,
  }));
  check('play starts a source', live.playing === true, JSON.stringify(live));
  await page.click('#btnBypass');
  await wait(120);
  check('bypass toggles', await page.evaluate(() => document.getElementById('btnBypass').classList.contains('on')));
  await page.click('#btnBypass');

  await page.evaluate(() => document.getElementById('presetSel').selectedIndex = 7);
  await page.evaluate(() => document.getElementById('presetSel').dispatchEvent(new Event('change')));
  await wait(400);
  const shot = path.join(SHOTS, 'fx-fracture.png');
  await page.screenshot({ path: shot, fullPage: true });
  console.log('  screenshot ' + path.relative(root, shot));
  check('no script errors during the whole run', pageErrors.length === 0, pageErrors.join(' | '));

  await page.click('#btnPlay');
  await browser.close();

  console.log(`\n${passed} assertions passed, ${failures.length} failed`);
  if (failures.length) { for (const f of failures) console.log('  - ' + f); process.exit(1); }
}
main().catch(e => { console.error(e); process.exit(1); });
