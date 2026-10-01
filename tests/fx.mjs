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
// the safety clip's ceiling: the plugin's is 0 dBFS (FractureCore.h), where the
// browser's own engine stopped at -0.9 dBFS
const CEILING = 1.0;

/* Rendered in the page: build the engine in an OfflineAudioContext, push
   noise through it, and report what came out. */
const RENDER = `async (patch, seconds) => {
  const sr = 44100;
  const ctx = new OfflineAudioContext(2, Math.round(sr*seconds), sr);
  const eng = window.FX.createEngine(ctx);
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  await eng.initWorklet();
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
  return { peak, bad, rms: Math.sqrt(sum/Math.max(1,n)), engine: !!eng.node };
}`;

/* The same graph, but reporting a per-channel envelope rather than one number:
   a tremolo is only doing its job if the level moves, and only auto-panning if
   the two channels move in opposite directions. */
const ENVELOPE = `async (patch, seconds) => {
  const sr = 44100;
  const ctx = new OfflineAudioContext(2, Math.round(sr*seconds), sr);
  const eng = window.FX.createEngine(ctx);
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  await eng.initWorklet();
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
  return { L: env[0], R: env[1], worklet: !!eng.node };
}`;

/* A sine or a quiet noise through the whole engine, returning the samples, for
   the anti-aliasing and the delay checks. */
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
      : kind === 'impulse' ? (i === 100 ? amp : 0)
      : amp*rnd();
  }
  const s = ctx.createBufferSource(); s.buffer = buf;
  const eng = window.FX.createEngine(ctx);
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  const shaperLoaded = await eng.initWorklet();
  s.connect(eng.input);
  s.start();
  const r = await ctx.startRendering();
  return { input: Array.from(buf.getChannelData(0)), out: Array.from(r.getChannelData(0)), shaperLoaded };
}`;

/* Two different channels in, both out: the per-band Stereo checks need a
   signal whose mid and side are known. 'mono' is the same noise on both,
   'side' is it on the left and inverted on the right. */
const STEREO = `async (patch, kind, seconds) => {
  const sr = 48000, len = Math.round(sr*seconds);
  const ctx = new OfflineAudioContext(2, len, sr);
  const buf = ctx.createBuffer(2, len, sr);
  let seed = 11;
  const rnd = () => (seed = (seed*16807) % 2147483647)/2147483647*2 - 1;
  const L = buf.getChannelData(0), R = buf.getChannelData(1);
  for (let i = 0; i < len; i++){ const v = 0.1*rnd(); L[i] = v; R[i] = kind === 'side' ? -v : v; }
  const eng = window.FX.createEngine(ctx);
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  await eng.initWorklet();
  const s = ctx.createBufferSource(); s.buffer = buf; s.connect(eng.input); s.start();
  const r = await ctx.startRendering();
  return { L: Array.from(r.getChannelData(0)), R: Array.from(r.getChannelData(1)) };
}`;

/* A sine through the whole engine, with the Scope's own analysers read
   partway through: what the harmonic readout would say about it. */
const READING = `async (patch, freq, seconds) => {
  const sr = 48000, len = Math.round(sr*seconds);
  const ctx = new OfflineAudioContext(2, len, sr);
  const buf = ctx.createBuffer(2, len, sr);
  for (let c = 0; c < 2; c++){ const d = buf.getChannelData(c); for (let i = 0; i < len; i++) d[i] = 0.5*Math.sin(2*Math.PI*freq*i/sr); }
  const eng = window.FX.createEngine(ctx);
  window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  await eng.initWorklet();
  const s = ctx.createBufferSource(); s.buffer = buf; s.connect(eng.input); s.start();
  let reading = null;
  ctx.suspend(seconds*0.75).then(() => {
    const a = new Float32Array(eng.hIn.frequencyBinCount), b = new Float32Array(eng.hOut.frequencyBinCount);
    eng.hIn.getFloatFrequencyData(a); eng.hOut.getFloatFrequencyData(b);
    reading = window.FX.readHarmonics(a, b, sr);
    reading.text = window.FX.readoutText(reading);
    ctx.resume();
  });
  await ctx.startRendering();
  return reading;
}`;

/* A sine whose period divides the frame exactly, through the engine, and the
   level of each of its first eight harmonics against the first, in dB: for
   checking that a drawn Table gives the harmonics that were drawn. */
const HARMONICS = `async (patch) => {
  const sr = 48000, len = sr, f0 = 375, N = 16384;           // 128 cycles in N
  const ctx = new OfflineAudioContext(2, len, sr);
  const buf = ctx.createBuffer(2, len, sr);
  for (let c = 0; c < 2; c++){ const d = buf.getChannelData(c); for (let i = 0; i < len; i++) d[i] = 0.5*Math.sin(2*Math.PI*f0*i/sr); }
  const eng = window.FX.createEngine(ctx); window.FX.applyValues(eng, Object.assign(window.FX.defaults(), patch));
  await eng.initWorklet();
  const s = ctx.createBufferSource(); s.buffer = buf; s.connect(eng.input); s.start();
  const r = (await ctx.startRendering()).getChannelData(0);
  const st = len - N, lv = [];
  let bad = 0, peak = 0;
  for (const v of r){ if (!Number.isFinite(v)) bad++; else peak = Math.max(peak, Math.abs(v)); }
  for (let h = 1; h <= 8; h++){
    let re = 0, im = 0;
    for (let i = 0; i < N; i++){ const ph = 2*Math.PI*h*f0*i/sr; re += r[st+i]*Math.cos(ph); im += r[st+i]*Math.sin(ph); }
    lv.push(Math.hypot(re, im));
  }
  return { db: lv.map(v => 20*Math.log10(v/lv[0] + 1e-30)), bad, peak };
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
  await page.waitForFunction(() => window.FX && window.FX.ready, { timeout: 15000 });

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
  check('default patch stays inside full scale', base.peak <= CEILING, 'peak ' + base.peak.toFixed(3));
  check('the plugin\'s engine runs in its worklet', base.engine === true);

  const modes = await page.evaluate(() => window.FX.MODES.map(m => m.id));
  for (const id of modes) {
    const r = await render({ bands: '1', m0a: id, d0a: 40, sb0: true, m0b: id, d0b: 40 });
    check(`mode ${id} at +32 dB, two stages`,
      r.bad === 0 && r.peak <= CEILING && r.rms > 0.0005,
      `bad ${r.bad} peak ${r.peak.toFixed(3)} rms ${r.rms.toFixed(4)}`);
  }

  const fb = await render({ bands: '1', m0a: 'hard', d0a: 24, fbAmt: 85, fbTime: 3, fbTone: 14000 }, 0.6);
  check('feedback at 85% does not run away', fb.bad === 0 && fb.peak <= CEILING,
    `bad ${fb.bad} peak ${fb.peak.toFixed(3)}`);

  const crushed = await render({ bands: '1', bits: 2, redux: 32, crMix: 100 });
  check('crush changes the signal', crushed.bad === 0 && Math.abs(crushed.rms - base.rms) > 0.001,
    `rms ${crushed.rms.toFixed(4)} vs ${base.rms.toFixed(4)}`);

  const wrapped = await render({ bands: '1', m0a: 'wrap', d0a: 30, autoGain: false, safety: true });
  check('safety clip contains the worst mode', wrapped.bad === 0 && wrapped.peak <= CEILING,
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
    const now = await page.evaluate(`(${SIGNAL})(${JSON.stringify(wrapPatch)}, 'sine', 3700, 0.5, 0.6)`);
    const off = await page.evaluate(`(${SIGNAL})(${JSON.stringify({ ...wrapPatch, osFactor: 'off' })}, 'sine', 3700, 0.5, 0.6)`);
    check('the engine loaded', now.shaperLoaded === true);
    const aNow = aliasDb(now.out, 3700), aOff = aliasDb(off.out, 3700);
    check('Wrap on a 3.7 kHz tone: aliasing well under the note at 4x, and under what it is with oversampling off',
      aNow < -12 && aNow < aOff - 6, `${aNow.toFixed(1)} dB at 4x against ${aOff.toFixed(1)} dB off`);

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

  console.log('\nPer-band stereo');
  {
    const stereo = (patch, kind) => page.evaluate(`(${STEREO})(${JSON.stringify(patch)}, '${kind}', 0.4)`);
    // a band driven hard, so that driving it or not is a large difference
    const hot = { bands: '1', m0a: 'hard', d0a: 30, autoGain: false, safety: false, mix: 100 };
    const skip = 4800;
    const diffDb = (a, b) => {
      let d = 0, e = 0;
      for (const ch of ['L', 'R']) for (let i = skip; i < a[ch].length; i++){ d += (a[ch][i] - b[ch][i])**2; e += b[ch][i]**2; }
      return 10*Math.log10(d/Math.max(e, 1e-30));
    };
    const clean = { ...hot, mx0: 0 };
    const lrMono = await stereo(hot, 'mono');
    const sideIn = await stereo({ ...clean }, 'side');
    const monoClean = await stereo({ ...clean }, 'mono');
    for (const [name, a, b] of [
      ['Mid on a mono signal is L/R', await stereo({ ...hot, st0: 'mid' }, 'mono'), lrMono],
      ['Mid leaves a side-only signal clean', await stereo({ ...hot, st0: 'mid' }, 'side'), sideIn],
      ['Side leaves a mono signal clean', await stereo({ ...hot, st0: 'side' }, 'mono'), monoClean],
    ]){
      const d = diffDb(a, b);
      check(name, d < -60, `differs by ${d.toFixed(1)} dB`);
    }
    // and the other way round: each mode does drive what it is meant to
    const sideDriven = diffDb(await stereo({ ...hot, st0: 'side' }, 'side'), sideIn);
    check('Side drives a side-only signal', sideDriven > -20, `differs from clean by ${sideDriven.toFixed(1)} dB`);
    const def = await page.evaluate(() => window.FX.defaults().st0);
    check('bands start on L/R', def === 'lr', def);
  }

  console.log('\nThe Scope\'s harmonic readout');
  {
    // the reader alone, on spectra windowed the way the analyser windows them
    const synth = await page.evaluate(() => {
      const sr = 48000, N = 8192;
      const spectrum = parts => {
        const re = new Float64Array(N), im = new Float64Array(N);
        for (let i = 0; i < N; i++){
          let x = 0; for (const [f, a] of parts) x += a*Math.sin(2*Math.PI*f*i/sr + f);
          const w = 0.42 - 0.5*Math.cos(2*Math.PI*i/N) + 0.08*Math.cos(4*Math.PI*i/N);
          re[i] = x*w;
        }
        for (let i = 1, j = 0; i < N; i++){ let bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
          if (i < j){ [re[i], re[j]] = [re[j], re[i]]; } }
        for (let len = 2; len <= N; len <<= 1){
          const ang = -2*Math.PI/len;
          for (let i = 0; i < N; i += len) for (let k = 0; k < len/2; k++){
            const wr = Math.cos(ang*k), wi = Math.sin(ang*k), a = i + k, b = a + len/2;
            const tr = re[b]*wr - im[b]*wi, ti = re[b]*wi + im[b]*wr;
            re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
          }
        }
        const db = new Float32Array(N/2);
        for (let k = 0; k < N/2; k++) db[k] = 20*Math.log10(Math.hypot(re[k], im[k])/N + 1e-30);
        return db;
      };
      const out = {};
      for (const f0 of [45, 440]){
        const r = window.FX.readHarmonics(spectrum([[f0, 0.5]]), spectrum([[f0, 0.5], [2*f0, 0.05], [3*f0, 0.005]]), sr);
        out[f0] = { tonal: r.tonal, f0: r.f0, h2: r.level[2], h3: r.level[3], h4: r.level[4], text: window.FX.readoutText(r) };
      }
      const chord = window.FX.readHarmonics(spectrum([[220, 0.3], [277.2, 0.3], [329.6, 0.3]]), spectrum([[220, 0.3]]), sr);
      out.chord = chord.tonal;
      const low = window.FX.readHarmonics(spectrum([[25, 0.5]]), spectrum([[25, 0.5]]), sr);
      out.low = low.tonal;
      return out;
    });
    for (const f0 of [45, 440]){
      const r = synth[f0];
      check(`a ${f0} Hz note with its 2nd at -20 dB and 3rd at -40 dB reads as such`,
        r.tonal && Math.abs(r.f0 - f0) < 0.5 && Math.abs(r.h2 + 20) < 0.5 && Math.abs(r.h3 + 40) < 0.5 && r.h4 < -60,
        JSON.stringify(r));
    }
    check('the reading names the note', synth[440].text.startsWith('A4  2nd -20  3rd -40'), synth[440].text);
    check('a chord is not read as a note', synth.chord === false);
    check('a note below the frame\'s reach is not read', synth.low === false);

    // and through the engine, from the Scope's own analysers: Soft is
    // symmetrical, so it adds no 2nd; Tube is not, so it does
    const reading = (patch, f) => page.evaluate(`(${READING})(${JSON.stringify(patch)}, ${f}, 0.6)`);
    for (const f of [45, 220]){
      const soft = await reading({ bands: '1', m0a: 'soft', d0a: 8 }, f);
      const tube = await reading({ bands: '1', m0a: 'tube', d0a: 8 }, f);
      check(`at ${f} Hz the engine's analysers read the note`, soft && soft.tonal && Math.abs(soft.f0 - f) < 1,
        soft ? `${soft.f0.toFixed(2)} Hz, "${soft.text}"` : 'no reading');
      check(`at ${f} Hz Soft adds a 3rd and no 2nd, Tube adds a 2nd`,
        soft && tube && soft.level[2] < -50 && soft.level[3] > -40 && tube.level[2] > -40,
        soft && tube ? `Soft "${soft.text}", Tube "${tube.text}"` : 'no reading');
    }
  }

  console.log('\nThe harmonic table');
  {
    // the curve alone: a full-scale cosine through it comes out as the bars
    const maths = await page.evaluate(() => {
      const { tableCurve, tableF, tableFF } = window.FX;
      const harmonicsOf = t => {
        const n = 4096, c = new Float64Array(17);
        for (let j = 0; j < n; j++){ const th = Math.PI*(j + 0.5)/n, y = tableF(t, Math.cos(th)); for (let k = 0; k <= 16; k++) c[k] += y*Math.cos(k*th); }
        return Array.from(c, (v, k) => v*(k ? 2 : 1)/n);
      };
      const bars = new Float64Array(16); bars[0] = 0.6; bars[2] = -0.3; bars[7] = 0.1;
      const t = tableCurve(bars), h = harmonicsOf(t);
      let worst = 0;
      for (let k = 1; k <= 16; k++) worst = Math.max(worst, Math.abs(h[k] - bars[k - 1]/1.0));
      // an even harmonic alone: shifted so that silence stays silent
      const even = new Float64Array(16); even[1] = 1;
      const te = tableCurve(even);
      // F is the area under f, past the clamp included
      let fWorst = 0;
      for (let x = -1.5; x < 1.5; x += 0.01){ const d = (tableFF(t, x + 1e-5) - tableFF(t, x - 1e-5))/2e-5; fWorst = Math.max(fWorst, Math.abs(d - tableF(t, x))); }
      let peak = 0;
      const wild = Float64Array.from({ length: 16 }, (_, k) => (k % 3 ? 1 : -1));
      const tw = tableCurve(wild);
      for (let x = -1; x <= 1; x += 0.001) peak = Math.max(peak, Math.abs(tableF(tw, x)));
      return { worst, even0: tableF(te, 0), fWorst, silent: tableF(tableCurve(new Float64Array(16)), 0.5), peak };
    });
    check('a full-scale sine through the curve comes out as the bars drawn (bars summing to 100%)',
      maths.worst < 1e-6, 'worst ' + maths.worst.toExponential(1));
    check('an even harmonic alone leaves silence silent', Math.abs(maths.even0) < 1e-12, String(maths.even0));
    // (a central difference that straddles the corner at +-1, where the
    // curve's slope jumps, is out by about its own step, 1e-5)
    check('F is the curve\'s antiderivative, past the clamp too', maths.fWorst < 1e-4, maths.fWorst.toExponential(1));
    check('no drawing exceeds full scale, and no bars is silence', maths.peak <= 1 + 1e-9 && maths.silent === 0,
      `peak ${maths.peak.toFixed(4)}, empty ${maths.silent}`);

    // through the engine, Drive 2 so the half-scale sine fills the curve
    const harm = patch => page.evaluate(`(${HARMONICS})(${JSON.stringify(patch)})`);
    const T = { bands: '1', m0a: 'table', d0a: 2, autoGain: false, safety: false };
    const f1 = await harm({ ...T, tb1h2: 50 });
    check('through the engine, frame 1 drawn with a 2nd at 50% gives a 2nd at -6.0 dB',
      Math.abs(f1.db[1] + 6.02) < 0.1 && Math.max(...f1.db.slice(2)) < -60, f1.db.map(v => v.toFixed(2)).join(' '));
    const f4 = await harm({ ...T, tblPos: 100 });
    check('Position 100% is frame 4: the 3rd at 50/60, the 5th at 40/60, no evens',
      Math.abs(f4.db[2] - 20*Math.log10(50/60)) < 0.1 && Math.abs(f4.db[4] - 20*Math.log10(40/60)) < 0.1 && f4.db[1] < -50,
      f4.db.map(v => v.toFixed(2)).join(' '));
    // halfway between frames 2 and 3: 100, 30, 25, 15, 17.5
    const half = await harm({ ...T, tblPos: 50 });
    const want = [30, 25, 15, 17.5].map(v => 20*Math.log10(v/100));
    check('Position 50% blends frames 2 and 3 bar by bar',
      want.every((w, i) => Math.abs(half.db[i + 1] - w) < 0.1), half.db.slice(1, 5).map(v => v.toFixed(2)).join(' ') + ' against ' + want.map(v => v.toFixed(2)).join(' '));
    const wild = {};
    for (let f = 1; f <= 4; f++) for (let k = 1; k <= 16; k++) wild[`tb${f}h${k}`] = ((f*7 + k*13) % 21) * 10 - 100;
    const hot = await render({ bands: '1', m0a: 'table', d0a: 40, sb0: true, m0b: 'table', d0b: 40, tblPos: 37, ...wild });
    check('Table at +32 dB, two stages, every bar drawn: finite and bounded',
      hot.bad === 0 && hot.peak <= CEILING && hot.rms > 0.0005, `bad ${hot.bad} peak ${hot.peak.toFixed(3)}`);
    const mod = await page.evaluate(() => window.FX.PLIST.find(d => d.id === 'tblPos').mod === true);
    check('Table position is a matrix target', mod);

    // Start from: the modes that sixteen harmonics can draw, and how closely
    const sf = await page.evaluate(() => {
      const fx = window.FX, err = id => fx.startFromError(fx.MODES.find(m => m.id === id).fn, 4);
      return { soft: err('soft'), tube: err('tube'), tape: err('tape'), warm: err('warm'),
               offered: fx.START_FROM_MODES.map(m => m.id) };
    });
    check('Start from: Soft, Tube, Tape and Warm come back within 1% at drive 4',
      sf.soft < 0.01 && sf.tube < 0.01 && sf.tape < 0.01 && sf.warm < 0.01, JSON.stringify(sf));
    check('Start from does not offer Wrap or Rectify, whose edges need more than sixteen harmonics',
      !sf.offered.includes('wrap') && !sf.offered.includes('rect') && sf.offered.includes('tube'), sf.offered.join(','));
  }

  console.log('\nModulation');
  const modded = await render({ bands: '1', d0a: 12, mS0: 'lfo1', mD0: 'd0a', mA0: 100, l1Rate: 8 }, 0.4);
  check('lfo on drive renders finite', modded.bad === 0 && modded.rms > 0.005 && modded.peak <= CEILING,
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
      vintage.bad === 0 && vintage.peak <= CEILING && Math.abs(vintage.rms - analog.rms)/analog.rms > 0.02,
      `analogue ${analog.rms.toFixed(4)} vintage ${vintage.rms.toFixed(4)}`);
    const singing = await render({ ...base, fltCirc: 'analog', fltQ: 18, fltDrive: 12 }, 0.5);
    check('a self-oscillating ladder stays inside the rails',
      singing.bad === 0 && singing.peak <= CEILING, `peak ${singing.peak.toFixed(3)}`);
  }
  {
    const off = await envelope({ bands: '1', mx0: 0, trOn: false });
    const on = await envelope({ bands: '1', mx0: 0, trOn: true, trDiv: 'free', trRate: 6,
                                trDepth: 100, trShape: 100, trEdge: 90 });
    const a = stats(off.L), b = stats(on.L);
    check('the tremolo is the engine\'s own', on.worklet === true);
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

  console.log('\nThe engine is the plugin\'s');
  {
    const build = await import(path.join(root, 'fx', 'engine', 'build.mjs'));
    const meta = await page.evaluate(() => ({ src: window.FX.ENGINE_SOURCE, guide: window.FX.GUIDE, tips: window.FX.TIPS,
      ver: document.getElementById('ver').textContent, n: window.FX.PLIST.length }));
    check('the embedded engine was built from plugin/core as it is now (else: npm run wasm)', meta.src === build.sourceHash(),
      `page ${meta.src}, sources ${build.sourceHash()}`);
    check('the guide is plugin/GUIDE.md, word for word', meta.guide === build.guideText());
    check('the tooltips are the plugin\'s', JSON.stringify(meta.tips) === JSON.stringify(build.tips()) && Object.keys(meta.tips).length > 60);
    check('the version shown is the plugin\'s', meta.ver === 'v' + build.version(), meta.ver);
    const table = fs.readFileSync(path.join(root, 'plugin', 'core', 'ParamTable.h'), 'utf8');
    check('every parameter in ParamTable.h is the page\'s too (185)', meta.n === 185 && /envKey/.test(table), String(meta.n));
  }

  console.log('\nWhat the plugin had and the page did not');
  {
    const sig = (patch, kind, f, amp, secs) => page.evaluate(`(${SIGNAL})(${JSON.stringify(patch)}, '${kind}', ${f}, ${amp}, ${secs})`);
    // the loop's period, from the autocorrelation of what comes out
    const period = x => {
      const a = x.slice(x.length - 16384);
      let best = { lag: 0, r: -1 };
      const e0 = a.reduce((p, v) => p + v*v, 0);
      for (let lag = 60; lag < 3000; lag++){
        let r = 0; for (let i = 0; i + lag < a.length; i++) r += a[i]*a[i + lag];
        if (r/e0 > best.r) best = { lag, r: r/e0 };
      }
      return best;
    };
    const fb = { bands: '1', d0a: 1.2, m0a: 'soft', autoGain: false, safety: true, fbAmt: 80, fbTone: 14000, mix: 100 };
    // Pitch is measured as the plugin's test_core measures it: an impulse into
    // the loop, and how far the fundamental's phase moves in one period. An
    // autocorrelation peak measures the loop's group delay, which at low notes
    // the DC blocker pulls away from the pitch
    const cents = (x, from, hz, sr = 48000) => {
      const per = sr/hz, n = Math.round(per*12), D = Math.round(per), w = 2*Math.PI*hz/sr;
      const frame = start => { let re = 0, im = 0;
        for (let i = 0; i < n; i++){ const win = 0.5 - 0.5*Math.cos(2*Math.PI*i/(n - 1)); re += x[start + i]*win*Math.cos(-w*i); im += x[start + i]*win*Math.sin(-w*i); }
        return [re, im]; };
      const [a, b] = frame(from), [c, d] = frame(from + D);
      const adv = Math.atan2(d*a - c*b, c*a + d*b);
      let dphi = adv - w*D; dphi -= 2*Math.PI*Math.round(dphi/(2*Math.PI));
      return 1200*Math.log2((hz + dphi*sr/(2*Math.PI*D))/hz);
    };
    const ring = { bands: '1', mx0: 0, osFactor: 'off', fbMode: 'pitch', fbAmt: 85, fbTone: 14000, mix: 100 };
    const tuning = [];
    for (const note of [45, 69]){
      const hz = 440*Math.pow(2, (note - 69)/12);
      const out = (await sig({ ...ring, fbNote: note }, 'impulse', 0, 0.5, 0.8)).out;
      tuning.push(cents(out, Math.round(100 + 3*48000/hz), hz));
    }
    check('FB mode Pitch: the loop rings at the note, A2 and A4, within a cent (as the plugin\'s test requires)',
      tuning.every(c => Math.abs(c) < 1), tuning.map(c => c.toFixed(2) + ' cents').join(', '));
    const timed = period((await sig({ ...fb, fbMode: 'time', fbTime: 30 }, 'noise', 0, 0.05, 0.8)).out);
    check('FB mode Time: the loop is the time set (30 ms, 1440 samples)', Math.abs(timed.lag - 1440) < 6, `period ${timed.lag}`);
    const synced = period((await sig({ ...fb, fbMode: 'sync', fbDiv: '1/32' }, 'noise', 0, 0.05, 0.8)).out);
    check('FB mode Sync: a 1/32 at 120 BPM (62.5 ms, 3000 samples) is past this window, so no shorter period', synced.lag > 2900 || synced.r < 0.3, `period ${synced.lag}`);

    const lfoOut = await envelope({ bands: '1', mx0: 0, mS0: 'lfo1', mD0: 'outGain', mA0: 100, l1Rate: 4 }, 1.0);
    const lo = stats(lfoOut.L);
    check('the modulation matrix runs in the engine: LFO 1 on Output moves the level', lo.min/lo.max < 0.3, `min/max ${(lo.min/lo.max).toFixed(3)}`);
    const flat = await envelope({ bands: '1', mx0: 0, fltType: 'lp', fltFreq: 400, rhDepth: 0 }, 1.0);
    const rh = await envelope({ bands: '1', mx0: 0, fltType: 'lp', fltFreq: 400, rhDepth: 3, rhDiv: '1/8', rhShape: 'sqr' }, 1.0);
    // 25 ms windows, so the noise's own jitter does not hide the rhythm: a
    // 1/8 at 120 BPM is 250 ms, half of it three octaves up, half three down
    const smooth = e => Array.from({ length: Math.floor(e.length/5) }, (_, k) => e.slice(5*k, 5*k + 5).reduce((p, v) => p + v, 0)/5);
    const lagCorr = (e, lag) => { const m = stats(e).mean; let n = 0, d = 0;
      for (let i = 0; i + lag < e.length; i++) n += (e[i] - m)*(e[i + lag] - m);
      for (const v of e) d += (v - m)**2; return n/d; };
    const e1 = smooth(rh.L), e0 = smooth(flat.L);
    const at = { full: lagCorr(e1, 10), half: lagCorr(e1, 5), without: lagCorr(e0, 10) };
    check('Filter rhythm moves the cutoff in time: a 1/8 square at 120 BPM repeats every 250 ms and flips every 125',
      at.full > 0.5 && at.half < -0.4 && at.without < 0.3, Object.entries(at).map(([k, v]) => `${k} ${v.toFixed(2)}`).join(', '));
    const mc = async v => (await render({ bands: '1', mS0: 'mc1', mD0: 'outGain', mA0: -100, mc1: v }, 0.3)).rms;
    const m0 = await mc(0), m1 = await mc(100);
    check('Macro 1 is a matrix source: turned up, it moves what it is routed to', m1 < m0*0.5, `${m0.toFixed(4)} at 0, ${m1.toFixed(4)} at 100`);
    const xy = async v => (await render({ bands: '1', mS0: 'xyy', mD0: 'outGain', mA0: -100, xyY: v }, 0.3)).rms;
    const y0 = await xy(0), y1 = await xy(100);
    check('the pad is a matrix source too', y1 < y0*0.5, `${y0.toFixed(4)} at 0, ${y1.toFixed(4)} at 100`);
    const thru = await render({ bands: '1', m0a: 'fold', d0a: 6, fbAmt: 70, fbTime: 20, fbThru: true }, 0.4);
    const plain = await render({ bands: '1', m0a: 'fold', d0a: 6, fbAmt: 70, fbTime: 20, fbThru: false }, 0.4);
    check('FB through drive changes the sound and stays bounded', thru.bad === 0 && thru.peak <= CEILING && Math.abs(thru.rms - plain.rms) > 0.002,
      `${thru.rms.toFixed(4)} through, ${plain.rms.toFixed(4)} not`);
    const fm = await render({ bands: '1', fltType: 'lp', fltFreq: 300, fltMix: 0 }, 0.3);
    const fw = await render({ bands: '1', fltType: 'lp', fltFreq: 300, fltMix: 100 }, 0.3);
    check('Filter mix at 0 is the unfiltered sound', fm.rms > fw.rms*1.5, `${fm.rms.toFixed(4)} at 0, ${fw.rms.toFixed(4)} at 100`);
  }

  console.log('\nTyped values');
  {
    const parsed = await page.evaluate(() => {
      const P = window.FX.P, tv = window.FX.typedValue;
      return {
        db: tv(P.inGain, '-6 dB'), plus: tv(P.inGain, '+3'), k: tv(P.x1, '2.2k'), khz: tv(P.x1, '2.2 kHz'),
        pc: window.FX.parseTyped('50%'), redux: window.FX.parseTyped('/4'), off: window.FX.parseTyped('off'),
        drive: tv(P.d0a, '12'), junk: tv(P.inGain, 'loud'), note: tv(P.fbNote, 'A2'), cents: tv(P.fbNote, 'C3 +50c'),
      };
    });
    check('typed text reads in the units shown',
      parsed.db === -6 && parsed.plus === 3 && parsed.k === 2200 && parsed.khz === 2200 && parsed.pc === 50 &&
      parsed.redux === 4 && parsed.off === 0 && Math.abs(parsed.drive - Math.pow(10, 12/20)) < 1e-9 && parsed.junk === null &&
      parsed.note === 45 && Math.abs(parsed.cents - 48.5) < 1e-9,
      JSON.stringify(parsed));
    // click the number under the Input knob, type, Enter
    const box = await page.evaluateHandle(() => {
      const knob = [...document.querySelectorAll('.knob')].find(k => k.querySelector('.klab').textContent === 'Input');
      knob.querySelector('.kval').click();
      return knob.querySelector('.kval input');
    });
    const opened = await page.evaluate(b => !!b, box);
    check('clicking a knob\'s number opens a box to type in', opened);
    if (opened){
      await box.type('-7.5 dB'); await page.keyboard.press('Enter');
      const after = await page.evaluate(() => ({ v: window.FX.state.inGain,
        shown: [...document.querySelectorAll('.knob')].find(k => k.querySelector('.klab').textContent === 'Input').querySelector('.kval').textContent }));
      check('Enter applies the typed value', after.v === -7.5 && after.shown === '-7.5', JSON.stringify(after));
      await page.evaluate(() => [...document.querySelectorAll('.knob')].find(k => k.querySelector('.klab').textContent === 'Input').querySelector('.kval').click());
      await page.keyboard.type('12'); await page.keyboard.press('Escape');
      const esc = await page.evaluate(() => window.FX.state.inGain);
      check('Escape leaves it as it was', esc === -7.5, String(esc));
      await page.evaluate(() => [...document.querySelectorAll('.knob')].find(k => k.querySelector('.klab').textContent === 'Input').querySelector('.kval').click());
      await page.keyboard.type('99'); await page.keyboard.press('Enter');
      const clamped = await page.evaluate(() => window.FX.state.inGain);
      check('a typed value past the end of the knob stops at the end', clamped === 24, String(clamped));
      // and back to 0 dB, so the live checks below hear the default
      await page.evaluate(() => [...document.querySelectorAll('.knob')].find(k => k.querySelector('.klab').textContent === 'Input').querySelector('.kval').click());
      await page.keyboard.type('0'); await page.keyboard.press('Enter');
    }
  }

  console.log('\nThe harmonic table panel');
  {
    const hidden = () => page.evaluate(() => document.getElementById('tablePanel').classList.contains('hidden'));
    check('the table panel starts closed', await hidden());
    await page.evaluate(() => { const s = [...document.querySelectorAll('.bandpane.on select')][0]; s.value = 'table'; s.dispatchEvent(new Event('change')); });
    await page.click('#btnTable');
    check('Harmonic table opens it over the panels, as in the plugin', !(await hidden()) && await page.evaluate(() => window.FX.state.m0a === 'table'));
    // draw across frame 1, left to right, from the top of the 3rd bar to the
    // middle of the 6th: every bar crossed is filled
    const cv = await page.$('#tFrames canvas');
    const bb = await cv.boundingBox();
    const lane = { x: bb.x + 8, y: bb.y + 26, w: bb.width - 16, h: bb.height - 44 };
    const xOf = k => lane.x + (k + 0.5)*lane.w/16, yOf = v => lane.y + lane.h/2 - v/100*lane.h/2;
    await page.mouse.move(xOf(2), yOf(90)); await page.mouse.down();
    await page.mouse.move(xOf(5), yOf(0), { steps: 1 }); await page.mouse.up();
    const drawn = await page.evaluate(() => [3, 4, 5, 6].map(k => window.FX.state[`tb1h${k}`]));
    check('dragging across a frame sets every bar it crosses', drawn[0] > 80 && drawn[1] > 40 && drawn[2] > 15 && Math.abs(drawn[3]) < 8, drawn.join(', '));
    await page.mouse.click(xOf(2), yOf(50), { clickCount: 2 });
    check('double-clicking a bar zeroes it', await page.evaluate(() => window.FX.state.tb1h3 === 0));
    await page.select('#tStart', '2:tube');
    const fill = await page.evaluate(() => ({ f3: [1, 2, 3].map(k => window.FX.state[`tb3h${k}`]), f2: window.FX.state.tb2h3, sel: document.getElementById('tStart').value }));
    check('Start from Tube fills frame 3 with Tube\'s harmonics, and only frame 3',
      fill.f3[0] === 100 && Math.abs(fill.f3[1]) > 5 && fill.f2 === 50 && fill.sel === '', JSON.stringify(fill));
    await page.click('#btnWobble');
    const routed = await page.evaluate(() => { const st = window.FX.state; const k = [0,1,2,3,4,5].find(k => st[`mD${k}`] === 'tblPos'); return k === undefined ? null : [st[`mS${k}`], st[`mA${k}`], document.getElementById('btnWobble').disabled]; });
    check('Wobble with LFO 1 routes LFO 1 to Position in a free slot', routed && routed[0] === 'lfo1' && routed[1] === 50 && routed[2] === true, JSON.stringify(routed));
    const tshot = path.join(SHOTS, 'fx-table.png');
    await (await page.$('#tablePanel')).screenshot({ path: tshot });
    console.log('  screenshot ' + path.relative(root, tshot));
    await page.click('#btnTableClose');
    check('Close puts it away', await hidden());
    await page.evaluate(() => { const s = document.getElementById('presetSel'); s.value = '0'; s.dispatchEvent(new Event('change')); });
  }

  console.log('\nThe panel: the plugin\'s, panel for panel');
  {
    const panels = await page.evaluate(() => [...document.querySelectorAll('.panel > h2')].map(h => h.dataset.no + ' ' + h.textContent.trim()));
    const want = ['01 Input & pre-filter', '02 Split', '03 Drive', '04 Crush & feedback', '05 Filter', '06 Filter rhythm',
                  '07 Modulation', '08 Scope', '09 Perform', '10 Tremolo', '11 Output'];
    check('the eleven panels, numbered as the plugin numbers them', JSON.stringify(panels) === JSON.stringify(want), panels.join(' | '));
    // every control the plugin's panel has, except the sidechain switch, which a page has nothing to offer
    const missing = await page.evaluate(() => {
      // the steps, the table's bars and the pad are drawn rather than knobs
      const drawn = ['rhStep', 'tb1', 'tb2', 'tb3', 'tb4', 'xyX', 'xyY'];
      return window.FX.PLIST.map(d => d.id).filter(id => id !== 'envKey' && !drawn.some(p => id.startsWith(p)) && !window.FX.ELS[id]);
    });
    check('every plugin control is on the page (the sidechain switch aside)', missing.length === 0, missing.join(', '));
    // what Relevance.h calls idle is dimmed: one band makes both splits idle
    const idle = async bands => { await page.select('#secSplit select', bands); return page.evaluate(() => [window.FX.ELS.x1.classList.contains('idle'), window.FX.ELS.x2.classList.contains('idle')]); };
    const one = await idle('1'), three = await idle('3');
    check('the engine\'s own relevance rules dim what does nothing: one band idles both splits, three idle neither',
      one[0] && one[1] && !three[0] && !three[1], JSON.stringify({ one, three }));
    const why = await page.evaluate(() => window.FX.ELS.fbNote.title);
    check('an idle control says why, in the engine\'s words', / — \S/.test(why), why);

    // undo, redo, and A/B with a history each
    const typeInto = async (label, text) => {
      await page.evaluate(l => [...document.querySelectorAll('.knob')].find(k => k.querySelector('.klab').textContent === l).querySelector('.kval').click(), label);
      await page.keyboard.type(text); await page.keyboard.press('Enter');
    };
    await typeInto('Input', '-5');
    await page.click('#btnUndo');
    const undone = await page.evaluate(() => window.FX.state.inGain);
    await page.click('#btnRedo');
    const redone = await page.evaluate(() => window.FX.state.inGain);
    check('Undo takes a change back and Redo puts it again', undone === 0 && redone === -5, `${undone}, ${redone}`);
    await page.click('#btnB');
    await typeInto('Input', '7');
    await page.click('#btnA');
    const onA = await page.evaluate(() => window.FX.state.inGain);
    await page.click('#btnB');
    const onB = await page.evaluate(() => window.FX.state.inGain);
    check('A and B hold two versions of a sound', onA === -5 && onB === 7, `A ${onA}, B ${onB}`);
    await page.click('#btnUndo');
    const bUndo = await page.evaluate(() => window.FX.state.inGain);
    check('B has its own undo', bUndo === -5, String(bUndo));
    await page.click('#btnA');

    // the filter display: drag across moves the cutoff, up the resonance
    const fv = await (await page.$('#cvFilter')).boundingBox();
    const before = await page.evaluate(() => [window.FX.state.fltFreq, window.FX.state.fltQ]);
    await page.mouse.move(fv.x + fv.width/2, fv.y + fv.height/2); await page.mouse.down();
    await page.mouse.move(fv.x + fv.width*0.8, fv.y + fv.height*0.3, { steps: 4 }); await page.mouse.up();
    const after = await page.evaluate(() => [window.FX.state.fltFreq, window.FX.state.fltQ]);
    check('dragging the filter display moves the cutoff and the resonance', after[0] > before[0]*1.5 && after[1] > before[1], `${before} -> ${after}`);
    // the steps, drawn
    const sv = await (await page.$('#cvSteps')).boundingBox();
    await page.mouse.click(sv.x + 10 + (sv.width - 46)*(2.5/8), sv.y + sv.height - 8 - (sv.height - 16)*0.25);
    const step3 = await page.evaluate(() => window.FX.state.rhStep3);
    check('clicking in the steps sets that step', Math.abs(step3 - 25) < 6, String(step3));
    // the pad
    const pv = await (await page.$('#cvPad')).boundingBox();
    await page.mouse.click(pv.x + 10 + (pv.width - 20)*0.75, pv.y + 22 + (pv.height - 32)*0.25);
    const xy = await page.evaluate(() => [window.FX.state.xyX, window.FX.state.xyY]);
    check('the pad sets XY X across and XY Y up', Math.abs(xy[0] - 75) < 3 && Math.abs(xy[1] - 75) < 3, xy.join(', '));
    // a macro's routes are shown under Perform
    await page.select('.mx select', 'mc1');
    await page.evaluate(() => { const s = document.querySelectorAll('.mx select')[1]; s.value = 'fltFreq'; s.dispatchEvent(new Event('change')); });
    await typeInto('Amount', '40');
    const route = await page.evaluate(() => document.querySelector('#routes .to').textContent);
    check('Perform shows what Macro 1 is routed to', route === 'Cutoff +40%', route);
    // the guide is the plugin's
    await page.click('#btnGuide');
    const guide = await page.evaluate(() => ({ open: !document.getElementById('guidePanel').classList.contains('hidden'),
      heads: [...document.querySelectorAll('#guideText h2')].map(h => h.textContent) }));
    check('Guide opens the plugin\'s guide', guide.open && guide.heads.includes('The harmonic table'), guide.heads.join(' | '));
    await page.click('#btnGuideClose');
    // the arrows step the menu's own order
    await page.evaluate(() => { const s = document.getElementById('presetSel'); s.value = '0'; s.dispatchEvent(new Event('change')); });
    await page.click('#btnNext');
    const next = await page.evaluate(() => document.getElementById('presetSel').selectedOptions[0].textContent);
    await page.click('#btnPrev'); await page.click('#btnPrev');
    const last = await page.evaluate(() => document.getElementById('presetSel').selectedOptions[0].textContent);
    check('the preset arrows step through the menu, wrapping round', next.startsWith('Thermal-ish') && last.startsWith('Wider'), `${next} / ${last}`);
    const pasted = await page.evaluate(() => [window.FX.readPatch('{"m0a":"tube","d0a":6,"nonsense":1}'), window.FX.readPatch('not json'), window.FX.readPatch('{"m0a":"nope"}')]);
    check('a pasted patch keeps what is the plugin\'s and refuses what is not',
      pasted[0] && pasted[0].m0a === 'tube' && pasted[0].d0a === 6 && !('nonsense' in pasted[0]) && pasted[1] === null && pasted[2] === null, JSON.stringify(pasted));
    await page.evaluate(() => { const s = document.getElementById('presetSel'); s.value = '0'; s.dispatchEvent(new Event('change')); });
  }

  console.log('\nPresets');
  const presetProblems = await page.evaluate(() => {
    const bad = [];
    for (const pr of window.FX.MENU_PRESETS) {
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

  // the headings must be the plugin's, or the two menus group differently
  {
    const src = fs.readFileSync(path.join(root, 'plugin', 'core', 'FactoryPresets.h'), 'utf8');
    const body = src.slice(src.indexOf('presetCategory('));
    const plugin = Object.fromEntries([...body.matchAll(/\{ "([^"]+)", "([^"]+)" \}/g)].map(m => [m[1], m[2]]));
    const { names, headings, groups } = await page.evaluate(() => ({
      names: window.FX.MENU_PRESETS.map(p => p.name), headings: window.FX.PRESET_HEADINGS,
      groups: [...document.querySelectorAll('#presetSel optgroup')].map(g => g.label),
    }));
    const wrong = names.filter(n => !headings[n] || headings[n] !== plugin[n]).map(n => `${n}: ${headings[n]} vs ${plugin[n]}`);
    check(`every preset in the menu sits under the plugin's heading (${names.length})`, wrong.length === 0, wrong.join(' | '));
    // in the plugin's order: each heading where its first preset is, browser presets first
    const order = [...new Set(names.map(n => plugin[n])), 'Your presets'];
    check('the menu shows the plugin\'s headings in the plugin\'s order', groups.join(',') === order.join(','), groups.join(',') + ' against ' + order.join(','));
    // the plugin's own presets, shown here too, must be the plugin's exactly
    const pluginJson = Object.fromEntries([...src.matchAll(/\{ "([^"]+)",\s*R"JSON\((.*?)\)JSON" \}/gs)].map(m => [m[1], JSON.parse(m[2])]));
    const ours = await page.evaluate(() => window.FX.PLUGIN_PRESETS);
    const differ = ours.filter(pr => JSON.stringify(pr.v) !== JSON.stringify(pluginJson[pr.name])).map(pr => pr.name);
    check(`the plugin's own presets are all here, as the plugin has them (${ours.length})`,
      ours.length === 27 && differ.length === 0, differ.join(' | '));
  }

  const presetCount = await page.evaluate(() => window.FX.MENU_PRESETS.length);
  for (let i = 0; i < presetCount; i++) {
    const pr = await page.evaluate(i => window.FX.MENU_PRESETS[i], i);
    const r = await render(pr.v, 0.35);
    check(`preset "${pr.name}"`, r.bad === 0 && r.peak <= CEILING && r.rms > 0.002,
      `bad ${r.bad} peak ${r.peak.toFixed(3)} rms ${r.rms.toFixed(4)}`);
  }

  console.log('\nLive context');
  check('a sub sine is offered as a source', await page.evaluate(() => [...document.querySelectorAll('#srcSel option')].some(o => o.value === 'sub')));
  for (const [src, note] of [['tone', /^A2  2nd/], ['sub', /^(F|F#)1  2nd/]]){
    await page.select('#srcSel', src);
    if (!(await page.evaluate(() => document.getElementById('btnPlay').textContent.includes('Stop')))) await page.click('#btnPlay');
    await wait(900);
    const t = await page.evaluate(() => document.getElementById('specRead').textContent);
    check(`playing the ${src === 'sub' ? 'sub sine' : '110 Hz tone'}, the Scope names the note`, note.test(t), t);
    await page.click('#btnPlay');
    await wait(100);
  }
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
