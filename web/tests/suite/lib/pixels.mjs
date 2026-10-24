import { inflateSync } from 'node:zlib';

// Read the composited screenshot. WebGPU's current canvas texture expires
// after presentation, so drawImage/getImageData can read an empty next buffer.
export function screenshotPixels(png) {
  if (!Buffer.isBuffer(png) || !png.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])))
    throw new Error('Invalid screenshot PNG signature');
  let width, height, channels, position = 8, ended = false;
  const chunks = [];
  while (position + 12 <= png.length) {
    const size = png.readUInt32BE(position);
    if (position + 12 + size > png.length) throw new Error('Truncated screenshot PNG chunk');
    const type = png.toString('ascii', position + 4, position + 8);
    const bytes = png.subarray(position + 8, position + 8 + size);
    if (type === 'IHDR') {
      if (position !== 8 || size !== 13) throw new Error('Invalid screenshot PNG header');
      width = bytes.readUInt32BE(0); height = bytes.readUInt32BE(4);
      channels = ({2: 3, 6: 4})[bytes[9]];
      if (!width || !height || bytes[8] !== 8 || !channels || bytes[10] || bytes[11] || bytes[12]) throw new Error('Unsupported screenshot PNG encoding');
    } else if (type === 'IDAT') chunks.push(bytes);
    else if (type === 'IEND') { ended = true; break; }
    position += size + 12;
  }
  if (!width || !height || !chunks.length || !ended) throw new Error('Incomplete screenshot PNG');
  const stride = width * channels;
  const raw = inflateSync(Buffer.concat(chunks));
  if (raw.length !== height * (stride + 1)) throw new Error('Invalid screenshot PNG scanline length');
  const rgb = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) for (let x = 0; x < stride; x++) {
    const left = x >= channels ? rgb[y * stride + x - channels] : 0;
    const above = y ? rgb[(y - 1) * stride + x] : 0;
    const corner = y && x >= channels ? rgb[(y - 1) * stride + x - channels] : 0;
    const p = left + above - corner;
    const a = Math.abs(p - left), b = Math.abs(p - above), c = Math.abs(p - corner);
    const predictor = [0, left, above, (left + above) >> 1, a <= b && a <= c ? left : b <= c ? above : corner][raw[y * (stride + 1)]];
    if (predictor === undefined) throw new Error('Invalid screenshot PNG row filter');
    rgb[y * stride + x] = (raw[y * (stride + 1) + x + 1] + predictor) & 255;
  }
  let lit = 0, varied = 0, redDominant = 0;
  const distinctColors = new Set();
  for (let i = 0; i < rgb.length; i += channels) {
    const [r, g, b] = rgb.subarray(i, i + 3);
    if (r + g + b > 30) lit++;
    if (Math.max(r, g, b) - Math.min(r, g, b) > 40) varied++;
    if (r > g + 20 && r > b + 20) redDominant++;
    distinctColors.add((r << 16) | (g << 8) | b);
  }
  return {
    width, height, lit, varied, redDominant,
    nonDarkPixelFraction: lit / (width * height),
    distinctColorCount: distinctColors.size,
  };
}
