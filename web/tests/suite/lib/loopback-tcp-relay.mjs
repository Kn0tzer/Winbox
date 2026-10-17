// Test-only byte-stream bridge. Every connection reaches the same owned
// loopback HTTP server; a browser cannot select a TCP destination.
import { connect } from 'node:net';
import { WebSocketServer } from 'ws';

export function attachLoopbackTcpRelay(server) {
  const address = server.address();
  if (!address || address.address !== '127.0.0.1' || !Number.isInteger(address.port))
    throw Error('Expected an already listening loopback HTTP test server');
  const targetPort = address.port;
  const origin = 'http://127.0.0.1:' + targetPort;
  const stats = { accepted: 0, rejected: 0, toTcpBytes: 0, toWebSocketBytes: 0 };
  const wss = new WebSocketServer({
    noServer: true, maxPayload: 1024 * 1024, perMessageDeflate: false,
    handleProtocols: protocols => protocols.has('binary') ? 'binary' : false,
  });
  const upstreams = new Set();
  const rejectedSockets = new Set();
  const upgrade = (request, socket, head) => {
    if (request.url !== '/runtime/__bw64_tcp' || request.headers.origin !== origin ||
        request.headers['sec-websocket-protocol'] !== 'binary' || wss.clients.size >= 8) {
      stats.rejected++;
      rejectedSockets.add(socket);
      const deadline = setTimeout(() => socket.destroy(), 1000);
      deadline.unref();
      socket.once('close', () => { clearTimeout(deadline); rejectedSockets.delete(socket); });
      socket.on('error', () => socket.destroy());
      socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n');
      return;
    }
    wss.handleUpgrade(request, socket, head, ws => wss.emit('connection', ws));
  };
  server.on('upgrade', upgrade);
  wss.on('connection', ws => {
    stats.accepted++;
    const upstream = connect({ host: '127.0.0.1', port: targetPort });
    upstreams.add(upstream);
    let sent = 0, received = 0;
    const close = () => {
      clearTimeout(deadline);
      upstream.destroy();
      if (ws.readyState !== 3) ws.terminate();
    };
    const deadline = setTimeout(close, 30000);
    deadline.unref();
    upstream.on('close', () => {
      upstreams.delete(upstream);
      clearTimeout(deadline);
      if (ws.readyState === 1) ws.close();
    });
    upstream.on('error', close);
    ws.on('error', close);
    ws.on('close', close);
    upstream.on('data', chunk => {
      received += chunk.length;
      if (received > 8 * 1024 * 1024 || ws.readyState !== 1) { close(); return; }
      stats.toWebSocketBytes += chunk.length;
      upstream.pause();
      ws.send(chunk, { binary: true }, error => {
        if (error) close(); else upstream.resume();
      });
    });
    ws.on('message', (data, binary) => {
      sent += data.length;
      if (!binary || sent > 8 * 1024 * 1024) { close(); return; }
      stats.toTcpBytes += data.length;
      if (!upstream.write(data)) ws.pause();
    });
    upstream.on('drain', () => ws.resume());
  });
  return {
    url: 'ws://127.0.0.1:' + targetPort + '/runtime/__bw64_tcp', stats,
    async close() {
      server.off('upgrade', upgrade);
      for (const socket of rejectedSockets) socket.destroy();
      for (const ws of wss.clients) ws.terminate();
      for (const socket of upstreams) socket.destroy();
      await new Promise(resolve => wss.close(resolve));
    },
  };
}
