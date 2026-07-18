import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Jimp } from 'jimp';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SRC = resolve(ROOT, 'contents/print.md');
const VERBS = resolve(ROOT, 'contents/verbs.txt');
const OUT = resolve(ROOT, 'sketch/content.h');

// Must match CONTENT_W in sketch/sketch.ino.
const CONTENT_W = 384;

const IMAGE_RE = /^!\[[^\]]*\]\(([^)]+)\)$/;

function fail(msg) {
  console.error(`gen-content: ${msg}`);
  process.exit(1);
}

function thresholdDither(lum) {
  const out = new Uint8Array(lum.length);
  for (let i = 0; i < lum.length; i++) out[i] = lum[i] < 128 ? 0 : 255;
  return out;
}

function floydSteinbergDither(lum, w, h) {
  const out = new Uint8Array(lum.length);
  const buf = Float32Array.from(lum);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x;
      const nv = buf[i] < 128 ? 0 : 255;
      const err = buf[i] - nv;
      out[i] = nv;
      if (x + 1 < w) buf[i + 1] += (err * 7) / 16;
      if (y + 1 < h) {
        if (x > 0) buf[i + w - 1] += (err * 3) / 16;
        buf[i + w] += (err * 5) / 16;
        if (x + 1 < w) buf[i + w + 1] += (err * 1) / 16;
      }
    }
  }
  return out;
}

const BAYER8 = [
  0, 32, 8, 40, 2, 34, 10, 42,
  48, 16, 56, 24, 50, 18, 58, 26,
  12, 44, 4, 36, 14, 46, 6, 38,
  60, 28, 52, 20, 62, 30, 54, 22,
  3, 35, 11, 43, 1, 33, 9, 41,
  51, 19, 59, 27, 49, 17, 57, 25,
  15, 47, 7, 39, 13, 45, 5, 37,
  63, 31, 55, 23, 61, 29, 53, 21,
];

function bayerDither(lum, w, h) {
  const out = new Uint8Array(lum.length);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const t = ((BAYER8[(y & 7) * 8 + (x & 7)] + 0.5) * 255) / 64;
      out[y * w + x] = lum[y * w + x] < t ? 0 : 255;
    }
  }
  return out;
}

const MODES = {
  standard: { label: 'standard (Bayer ordered dither)', fn: bayerDither },
  grayscale: { label: 'grayscale (Floyd-Steinberg error diffusion)', fn: floydSteinbergDither },
  mono: { label: 'mono (threshold)', fn: thresholdDither },
};

// Output is a packed 1bpp bitmap (MSB-first rows, bit 1 = black) drawn
// on-device with drawBitmap(). Not PNG: LovyanGFX's drawPng() mangles colors
// on the sketch's 1bpp palette sprite (everything comes out black).
async function processImage(path, mode) {
  let img;
  try {
    img = await Jimp.read(path);
  } catch (e) {
    fail(`cannot read image (PNG/JPEG only): ${path}\n  ${e.message}`);
  }

  const srcW = img.bitmap.width;
  const srcH = img.bitmap.height;

  let resized = false;
  if (srcW > CONTENT_W) {
    img.resize({ w: CONTENT_W });
    resized = true;
  }

  const { width: w, height: h, data } = img.bitmap;

  const lum = new Float32Array(w * h);
  for (let p = 0, i = 0; p < w * h; p++, i += 4) {
    const a = data[i + 3] / 255;
    const r = data[i] * a + 255 * (1 - a);
    const g = data[i + 1] * a + 255 * (1 - a);
    const b = data[i + 2] * a + 255 * (1 - a);
    lum[p] = 0.299 * r + 0.587 * g + 0.114 * b;
  }

  const bw = MODES[mode].fn(lum, w, h);

  const rowBytes = (w + 7) >> 3;
  const packed = new Uint8Array(rowBytes * h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      if (bw[y * w + x] === 0) {
        packed[y * rowBytes + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
  }

  return { buf: Buffer.from(packed), w, h, srcW, srcH, resized };
}

// Candidates for {{verb}}: one per line, blank/#/non-ASCII lines ignored.
function loadVerbs() {
  let raw;
  try {
    raw = readFileSync(VERBS, 'utf8');
  } catch {
    return [];
  }
  return raw.split(/\r?\n/)
    .map((l) => l.trim())
    .filter((l) => l && !l.startsWith('#') && /^[\x20-\x7e]+$/.test(l));
}

function cString(s) {
  return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function hexDump(buf, indent = '  ') {
  const out = [];
  for (let i = 0; i < buf.length; i += 12) {
    const row = [...buf.subarray(i, i + 12)].map((b) => `0x${b.toString(16).padStart(2, '0')}`);
    out.push(indent + row.join(', ') + (i + 12 < buf.length ? ',' : ''));
  }
  return out.join('\n');
}

function parseArgs() {
  const args = process.argv.slice(2);
  let mode = 'standard';
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--mode') mode = args[++i];
    else if (args[i].startsWith('--mode=')) mode = args[i].slice('--mode='.length);
    else fail(`unknown argument: ${args[i]}`);
  }
  if (!Object.hasOwn(MODES, mode)) {
    fail(`--mode must be one of ${Object.keys(MODES).join(' / ')} (got: ${mode})`);
  }
  return mode;
}

const mode = parseArgs();

const raw = readFileSync(SRC, 'utf8');
const lines = raw.split(/\r?\n/);
while (lines.length && lines[lines.length - 1] === '') lines.pop();

const verbs = loadVerbs();
if (raw.includes('{{verb}}') && verbs.length === 0) {
  fail(`print.md uses {{verb}} but contents/verbs.txt has no usable lines`);
}

const images = [];
const blocks = [];

for (const line of lines) {
  const m = IMAGE_RE.exec(line.trim());
  if (!m) {
    blocks.push({ type: 'text', text: line });
    continue;
  }
  const imgPath = resolve(dirname(SRC), m[1]);
  const info = await processImage(imgPath, mode);
  blocks.push({ type: 'image', index: images.length });
  images.push({ ...info, src: relative(ROOT, imgPath).replace(/\\/g, '/') });
}

const parts = [];
parts.push(`// AUTO-GENERATED by tools/gen-content.mjs -- DO NOT EDIT`);
parts.push(`#pragma once`);
parts.push(``);
parts.push(`#include <stddef.h>`);
parts.push(`#include <stdint.h>`);
parts.push(``);
parts.push(`enum content_block_type_t : uint8_t {`);
parts.push(`  CONTENT_BLOCK_TEXT = 0,`);
parts.push(`  CONTENT_BLOCK_IMAGE = 1,`);
parts.push(`};`);
parts.push(``);
parts.push(`struct content_block_t {`);
parts.push(`  content_block_type_t type;`);
parts.push(`  const char* text;`);
parts.push(`  const uint8_t* image_data;`);
parts.push(`  uint32_t image_len;`);
parts.push(`  uint16_t image_w;`);
parts.push(`  uint16_t image_h;`);
parts.push(`};`);
parts.push(``);

for (const [i] of images.entries()) {
  parts.push(`static const uint8_t content_image_${i}[] = {`);
  parts.push(hexDump(images[i].buf));
  parts.push(`};`);
  parts.push(``);
}

parts.push(`static const content_block_t content_blocks[] = {`);
for (const b of blocks) {
  if (b.type === 'image') {
    const img = images[b.index];
    parts.push(`  { CONTENT_BLOCK_IMAGE, nullptr, content_image_${b.index}, ` +
      `${img.buf.length}, ${img.w}, ${img.h} },`);
  } else {
    parts.push(`  { CONTENT_BLOCK_TEXT, "${cString(b.text)}", nullptr, 0, 0, 0 },`);
  }
}
parts.push(`};`);
parts.push(``);
parts.push(`static const size_t content_block_count = sizeof(content_blocks) / sizeof(content_blocks[0]);`);
parts.push(``);
if (verbs.length > 0) {
  parts.push(`static const char* const content_verbs[] = {`);
  for (const v of verbs) parts.push(`  "${cString(v)}",`);
  parts.push(`};`);
  parts.push(`static const size_t content_verb_count = sizeof(content_verbs) / sizeof(content_verbs[0]);`);
} else {
  parts.push(`static const char* const content_verbs[1] = { "" };`);
  parts.push(`static const size_t content_verb_count = 0;`);
}
parts.push(``);

// Must stay BOM-free UTF-8; the content carries Japanese and gcc rejects a BOM.
writeFileSync(OUT, parts.join('\n'), 'utf8');

const textCount = blocks.filter((b) => b.type === 'text').length;
console.log(`gen-content: ${relative(ROOT, SRC).replace(/\\/g, '/')} -> ` +
  `${relative(ROOT, OUT).replace(/\\/g, '/')}`);
console.log(`  image mode: ${MODES[mode].label}`);
console.log(`  ${textCount} text lines / ${images.length} images / ${verbs.length} verbs`);
for (const img of images) {
  const from = img.resized ? `${img.srcW}x${img.srcH} -> ${img.w}x${img.h} resized` : `${img.w}x${img.h}`;
  console.log(`  image: ${img.src} ${from} ${img.buf.length} bytes`);
}
