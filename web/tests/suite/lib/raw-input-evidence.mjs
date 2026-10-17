// Wine can report a debug string through both its guest fd and debug logger.
// Packet sequence numbers, rather than log offsets, identify delivered input.
export const rawEvents = lines => [...new Map(lines
  .map(line => line.match(/input-witness: RAW seq=(\d+) flags=(\d+) dx=(-?\d+) dy=(-?\d+)/))
  .filter(Boolean)
  .map(m => [m[1], { sequence: +m[1], flags: +m[2], dx: +m[3], dy: +m[4] }])).values()];

export const rawEventAfter = (sequence, dx, dy) => ({
  test: line => rawEvents([line]).some(event => event.sequence > sequence && event.dx === dx && event.dy === dy),
  toString: () => `RAW sequence > ${sequence}, dx=${dx}, dy=${dy}`,
});
