// Renders the CLDM icons (a glowing download arrow) to extension/icons/*.png.
// Usage: node tools/make-icons.js   (no dependencies)
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) {
    c = (crc ^ buf[n]) & 0xff;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function png(size, rgba) {
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const raw = Buffer.alloc(size * (size * 4 + 1));
  for (let y = 0; y < size; y++) rgba.copy(raw, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)),
  ]);
}

function segDist(px, py, ax, ay, bx, by) {
  const dx = bx - ax, dy = by - ay;
  const t = Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)));
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Shapes in a 0..1 coordinate space.
const SEGMENTS = [
  [0.5, 0.2, 0.5, 0.6],     // shaft
  [0.32, 0.44, 0.5, 0.62],  // arrow head
  [0.68, 0.44, 0.5, 0.62],
  [0.26, 0.78, 0.74, 0.78], // tray
];
const CYAN = [0, 240, 255], GREEN = [57, 255, 20], BG = [6, 10, 12];

function render(size) {
  const out = Buffer.alloc(size * size * 4);
  const ss = 4; // supersampling
  const stroke = size <= 16 ? 0.075 : 0.055;
  const radius = 0.22;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      let r = 0, g = 0, b = 0, a = 0;
      for (let sy = 0; sy < ss; sy++) {
        for (let sx = 0; sx < ss; sx++) {
          const u = (x + (sx + 0.5) / ss) / size, v = (y + (sy + 0.5) / ss) / size;
          // Rounded-square background.
          const qx = Math.max(Math.abs(u - 0.5) - (0.5 - radius), 0);
          const qy = Math.max(Math.abs(v - 0.5) - (0.5 - radius), 0);
          if (Math.hypot(qx, qy) > radius) continue;
          let d = Infinity;
          for (const s of SEGMENTS) d = Math.min(d, segDist(u, v, ...s));
          const t = Math.min(1, Math.max(0, (v - 0.2) / 0.6));
          const col = CYAN.map((c, i) => c + (GREEN[i] - c) * t);
          const core = d < stroke / 2 ? 1 : 0;
          const glow = Math.exp(-Math.max(0, d - stroke / 2) * (size <= 16 ? 40 : 18)) * 0.8;
          const k = Math.max(core, glow);
          const px = BG.map((c, i) => c + (col[i] - c) * k);
          if (core) { px[0] += (255 - px[0]) * 0.25; px[1] += (255 - px[1]) * 0.25; px[2] += (255 - px[2]) * 0.25; }
          r += px[0]; g += px[1]; b += px[2]; a += 255;
        }
      }
      const n = ss * ss, o = (y * size + x) * 4;
      const cov = a / 255;
      out[o] = cov ? r / cov : 0; out[o + 1] = cov ? g / cov : 0; out[o + 2] = cov ? b / cov : 0; out[o + 3] = a / n;
    }
  }
  return out;
}

const dir = path.join(__dirname, '..', 'extension', 'icons');
fs.mkdirSync(dir, { recursive: true });
for (const size of [16, 32, 48, 128]) {
  fs.writeFileSync(path.join(dir, `icon${size}.png`), png(size, render(size)));
}
console.log('icons written to', dir);
