/**
 * Draws the disk image's background — Béton clair, no text: the Finder already
 * prints every file's name under its icon, so the picture only has to do the
 * pointing. Flat colour, hard edges, one arrow.
 *
 *   node plugin/packaging/make-background.mjs out.png
 *
 * The PNG encoder is the repository's own (scripts/lib/png.mjs), so this adds no
 * dependency and no binary to the repository: the background is generated at
 * packaging time like the extension icons are.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodePNG } from '../../scripts/lib/png.mjs';

const W = 720, H = 480, S = 2;                 // drawn at 2x, box-filtered down
const px = new Float64Array(W * S * H * S * 4);

const hex = h => {
  const v = h.replace('#', '');
  return [0, 2, 4].map(i => parseInt(v.slice(i, i + 2), 16) / 255);
};
const ground = hex('#d6d1c6'), face = hex('#f4f1ea'), ink = hex('#17150f');
const yellow = hex('#e9b21f'), red = hex('#c0392f'), blue = hex('#1e4b8f');

function plot(x, y, colour, a = 1) {
  if (a <= 0 || x < 0 || y < 0 || x >= W * S || y >= H * S) return;
  const i = (y * W * S + x) * 4;
  px[i] = colour[0] * a + px[i] * (1 - a);
  px[i + 1] = colour[1] * a + px[i + 1] * (1 - a);
  px[i + 2] = colour[2] * a + px[i + 2] * (1 - a);
  px[i + 3] = a + px[i + 3] * (1 - a);
}
const rect = (x, y, w, h, colour) => {
  for (let j = Math.round(y * S); j < Math.round((y + h) * S); j++)
    for (let i = Math.round(x * S); i < Math.round((x + w) * S); i++) plot(i, j, colour);
};
const disc = (cx, cy, r, colour) => {
  for (let j = Math.round((cy - r) * S); j <= Math.round((cy + r) * S); j++)
    for (let i = Math.round((cx - r) * S); i <= Math.round((cx + r) * S); i++) {
      const dx = (i + 0.5) / S - cx, dy = (j + 0.5) / S - cy;
      const d = Math.hypot(dx, dy);
      if (d <= r - 0.5) plot(i, j, colour);
      else if (d < r + 0.5) plot(i, j, colour, r + 0.5 - d);   // one pixel of antialiasing
    }
};
// a square with its top-left corner rounded off, the plugin's third mark
const quarter = (x, y, size, colour) => {
  const r = size;
  for (let j = Math.round(y * S); j < Math.round((y + size) * S); j++)
    for (let i = Math.round(x * S); i < Math.round((x + size) * S); i++) {
      const cx = (i + 0.5) / S, cy = (j + 0.5) / S;
      if (cx < x + r && cy < y + r){
        const d = Math.hypot(cx - (x + r), cy - (y + r));
        if (d <= r - 0.5) plot(i, j, colour);
        else if (d < r + 0.5) plot(i, j, colour, r + 0.5 - d);
      } else plot(i, j, colour);
    }
};
// a blunt arrow pointing right, the width of a shaft plus a solid head
const arrow = (x, y, len, colour) => {
  const shaft = 10, head = 26;
  rect(x, y - shaft / 2, len - head, shaft, colour);
  for (let j = Math.round((y - head / 2) * S); j <= Math.round((y + head / 2) * S); j++) {
    const t = Math.abs((j + 0.5) / S - y) / (head / 2);
    const w = (1 - t) * head;
    for (let i = Math.round((x + len - head) * S); i < Math.round((x + len - head + w) * S); i++)
      plot(i, j, colour);
  }
};

rect(0, 0, W, H, ground);

// the colour ribbon, as on the plugin's own header
rect(0, 0, W - 220, 10, yellow);
rect(W - 220, 0, 110, 10, red);
rect(W - 110, 0, 70, 10, blue);
rect(W - 40, 0, 40, 10, ink);

// the three marks
rect(30, 34, 30, 30, ink);
disc(83, 49, 15, red);
quarter(105, 34, 30, blue);

// a plaster field behind the installer, so the eye lands on it first
rect(96, 150, 200, 130, face);
rect(96, 150, 200, 6, yellow);

// and behind the read-me, quieter
rect(430, 150, 200, 130, face);

// the app drags to Applications
arrow(455, 372, 96, ink);

// downsample
const out = Buffer.alloc(W * H * 4);
for (let y = 0; y < H; y++)
  for (let x = 0; x < W; x++) {
    let r = 0, g = 0, b = 0, a = 0;
    for (let sy = 0; sy < S; sy++)
      for (let sx = 0; sx < S; sx++) {
        const i = ((y * S + sy) * W * S + (x * S + sx)) * 4;
        r += px[i]; g += px[i + 1]; b += px[i + 2]; a += px[i + 3];
      }
    const n = S * S;
    r /= n; g /= n; b /= n; a /= n;
    const o = (y * W + x) * 4;
    const q = v => Math.max(0, Math.min(255, Math.round((a > 0 ? v / a : 0) * 255)));
    out[o] = q(r); out[o + 1] = q(g); out[o + 2] = q(b);
    out[o + 3] = Math.round(a * 255);
  }

const dest = process.argv[2]
  || path.join(path.dirname(fileURLToPath(import.meta.url)), 'dmg-background.png');
fs.writeFileSync(dest, encodePNG(W, H, out));
console.log(`${dest} — ${W}x${H}`);
