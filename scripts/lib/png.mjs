/**
 * A minimal PNG writer, and just enough of a rasteriser to draw the extension
 * icon. This exists so the repository stays text-only: the icons are generated
 * at build time rather than committed as binaries, which keeps the whole tool
 * reviewable by reading it. Nobody has to trust a PNG they cannot diff.
 *
 * Node's zlib does the compression, so there is no dependency here either.
 */
import zlib from 'node:zlib';

/* ---------- PNG container ---------- */

const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return t;
})();

export function crc32(buf) {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

/** Encode straight RGBA bytes (width*height*4) as an 8-bit RGBA PNG. */
export function encodePNG(width, height, rgba) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 6;   // colour type: truecolour with alpha
  ihdr[10] = 0;  // deflate
  ihdr[11] = 0;  // adaptive filtering
  ihdr[12] = 0;  // no interlace

  // One filter byte per scanline. Filter 0 (none) compresses well enough at
  // these sizes and keeps this readable.
  const stride = width * 4;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (stride + 1)] = 0;
    rgba.copy
      ? rgba.copy(raw, y * (stride + 1) + 1, y * stride, y * stride + stride)
      : Buffer.from(rgba).copy(raw, y * (stride + 1) + 1, y * stride, y * stride + stride);
  }

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

/* ---------- tiny rasteriser ---------- */

/**
 * A supersampled canvas. Everything is drawn at `scale` times the requested
 * size and box-filtered down, which is what gives the small icons usable
 * edges — a 16px icon drawn without antialiasing looks broken in a toolbar.
 */
export class Canvas {
  constructor(size, scale = 4) {
    this.size = size;
    this.scale = scale;
    this.dim = size * scale;
    this.px = new Float64Array(this.dim * this.dim * 4); // premultiplied, 0..1
  }

  /** Source-over composite of one pixel. */
  #blend(x, y, [r, g, b], a) {
    if (a <= 0 || x < 0 || y < 0 || x >= this.dim || y >= this.dim) return;
    const i = (y * this.dim + x) * 4;
    const p = this.px;
    p[i] = r * a + p[i] * (1 - a);
    p[i + 1] = g * a + p[i + 1] * (1 - a);
    p[i + 2] = b * a + p[i + 2] * (1 - a);
    p[i + 3] = a + p[i + 3] * (1 - a);
  }

  /**
   * Filled rounded rectangle, in unscaled user units. `colour` is [r,g,b] with
   * components 0..1; `alpha` defaults to opaque.
   */
  roundRect(x, y, w, h, radius, colour, alpha = 1) {
    const s = this.scale;
    const [X, Y, W, H, R] = [x * s, y * s, w * s, h * s, radius * s];
    const x0 = Math.max(0, Math.floor(X)), x1 = Math.min(this.dim, Math.ceil(X + W));
    const y0 = Math.max(0, Math.floor(Y)), y1 = Math.min(this.dim, Math.ceil(Y + H));

    for (let py = y0; py < y1; py++) {
      for (let px = x0; px < x1; px++) {
        // Distance from the rounded-rect boundary, sampled at pixel centre.
        const cx = px + 0.5, cy = py + 0.5;
        const dx = Math.max(X + R - cx, 0, cx - (X + W - R));
        const dy = Math.max(Y + R - cy, 0, cy - (Y + H - R));
        const inside = cx >= X && cx <= X + W && cy >= Y && cy <= Y + H;
        if (!inside) continue;
        if (dx > 0 && dy > 0 && Math.hypot(dx, dy) > R) continue;
        this.#blend(px, py, colour, alpha);
      }
    }
  }

  /** Box-downsample to the requested size and return straight RGBA bytes. */
  toRGBA() {
    const { size, scale, dim, px } = this;
    const out = Buffer.alloc(size * size * 4);
    const n = scale * scale;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        let r = 0, g = 0, b = 0, a = 0;
        for (let sy = 0; sy < scale; sy++) {
          for (let sx = 0; sx < scale; sx++) {
            const i = ((y * scale + sy) * dim + (x * scale + sx)) * 4;
            r += px[i]; g += px[i + 1]; b += px[i + 2]; a += px[i + 3];
          }
        }
        r /= n; g /= n; b /= n; a /= n;
        const o = (y * size + x) * 4;
        // Un-premultiply for storage.
        const q = v => Math.max(0, Math.min(255, Math.round((a > 0 ? v / a : 0) * 255)));
        out[o] = q(r); out[o + 1] = q(g); out[o + 2] = q(b);
        out[o + 3] = Math.max(0, Math.min(255, Math.round(a * 255)));
      }
    }
    return out;
  }

  toPNG() {
    return encodePNG(this.size, this.size, this.toRGBA());
  }
}

export const hex = h => {
  const v = h.replace('#', '');
  return [0, 2, 4].map(i => parseInt(v.slice(i, i + 2), 16) / 255);
};
