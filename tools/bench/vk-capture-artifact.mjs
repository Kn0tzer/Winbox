// Export one same-presentation capture; raw files and hashes are authoritative.
// PNGs for float targets clamp linear values to [0,1] as diagnostic previews.
// node tools/bench/vk-capture-artifact.mjs capture.json output-directory
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { deflateSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { v2DiagBytesPerTexel, v2DiagFormatKind } from '../../web/vkwebgpu.mjs';

function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) { c ^= b; for (let i = 0; i < 8; i++) c = (c >>> 1) ^ ((c & 1) ? 0xedb88320 : 0); }
  return (c ^ 0xffffffff) >>> 0;
}
export function rgbaPng(w, h, rgba) {
  if (rgba.length !== w * h * 4) throw new Error('PNG pixel extent mismatch');
  const chunk = (type, data) => {
    const body = Buffer.concat([Buffer.from(type), data]), head = Buffer.alloc(4), tail = Buffer.alloc(4);
    head.writeUInt32BE(data.length); tail.writeUInt32BE(crc32(body));
    return Buffer.concat([head, body, tail]);
  };
  const header = Buffer.alloc(13); header.writeUInt32BE(w); header.writeUInt32BE(h, 4); header[8] = 8; header[9] = 6;
  const scan = Buffer.alloc(h * (w * 4 + 1));
  for (let y = 0; y < h; y++) scan.set(rgba.subarray(y * w * 4, (y + 1) * w * 4), y * (w * 4 + 1) + 1);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header), chunk('IDAT', deflateSync(scan)), chunk('IEND', Buffer.alloc(0))]);
}
const half = v => { const s = v & 32768 ? -1 : 1, e = (v >> 10) & 31, m = v & 1023; return e === 0 ? s * m * 2 ** -24 : e === 31 ? m ? NaN : s * Infinity : s * (1 + m / 1024) * 2 ** (e - 15); };
export function unpackCaptureImage(image) {
  if (!image.data || !Object.hasOwn(image.data, '$bytes')) return null;
  const bpt = v2DiagBytesPerTexel(image.format), kind = v2DiagFormatKind(image.format);
  if (!bpt) throw new Error('Unsupported capture format ' + image.format);
  const rows = Buffer.from(image.data.$bytes, 'base64');
  if (rows.length !== image.bytesPerRow * image.h || image.bytesPerRow < image.w * bpt) throw new Error('Capture pixel extent mismatch');
  const raw = Buffer.alloc(image.w * image.h * bpt), rgba = Buffer.alloc(image.w * image.h * 4);
  for (let y = 0; y < image.h; y++) raw.set(rows.subarray(y * image.bytesPerRow, y * image.bytesPerRow + image.w * bpt), y * image.w * bpt);
  for (let p = 0; p < image.w * image.h; p++) {
    let color = [0, 0, 0, 255]; const o = p * bpt;
    if (kind === 'rgba8' || kind === 'bgra8') color = kind === 'bgra8' ? [raw[o + 2], raw[o + 1], raw[o], raw[o + 3]] : [...raw.subarray(o, o + 4)];
    else if (kind === 'r8' || kind === 'rg8') { color[0] = raw[o]; if (kind === 'rg8') color[1] = raw[o + 1]; }
    else {
      const step = kind.endsWith('16f') ? 2 : 4, count = bpt / step;
      for (let c = 0; c < count; c++) { const v = step === 2 ? half(raw.readUInt16LE(o + c * step)) : raw.readFloatLE(o + c * step); color[c] = Math.round(Math.min(1, Math.max(0, v || 0)) * 255); }
    }
    rgba.set(color, p * 4);
  }
  return { raw, rgba, sha256: createHash('sha256').update(raw).digest('hex') };
}
export async function writeVkCapture(dir, report) {
  await mkdir(dir, { recursive: true });
  const images = [];
  for (const [index, image] of (report.images || []).entries()) {
    const pixels = unpackCaptureImage(image);
    const name = `${index}-submission-${image.submission}-${image.stage}${image.pass === undefined ? '' : '-pass-' + image.pass}-${image.slot ?? 0}`;
    if (pixels) {
      await writeFile(join(dir, name + '.bin'), pixels.raw);
      await writeFile(join(dir, name + '.png'), rgbaPng(image.w, image.h, pixels.rgba));
    }
    images.push({ ...image, data: undefined, rawFile: pixels ? name + '.bin' : null, pngFile: pixels ? name + '.png' : null, sha256: pixels?.sha256 });
  }
  await writeFile(join(dir, 'capture.json'), JSON.stringify(report) + '\n');
  await writeFile(join(dir, 'index.json'), JSON.stringify({ ...report, wire: undefined, decoded: undefined, images }, null, 2) + '\n');
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv.length !== 4) throw new Error('Usage: vk-capture-artifact.mjs capture.json output-directory');
  await writeVkCapture(resolve(process.argv[3]), JSON.parse(await readFile(process.argv[2], 'utf8')));
}
