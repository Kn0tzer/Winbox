/** Extract a source region only when its markers still exist and remain ordered. */
export function extractSourceSlice(source, startMarker, endMarker, { sourceName = '<source>' } = {}) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start < 0 ? 0 : start + startMarker.length);
  if (start < 0 || end < 0 || end < start) {
    const missing = start < 0 ? startMarker : endMarker;
    throw new Error(`source slice marker not found in ${sourceName}: ${missing}`);
  }
  return source.slice(start, end);
}
