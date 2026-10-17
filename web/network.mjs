export const wispEndpoint = 'wss://kn0tzer.work.gd/wss/';

export function dnsResponse(query, dns) {
  if (query.length < 12 || query.length > 4096 || query[4] !== 0 || query[5] !== 1) throw Error('Invalid DNS question');
  const labels = []; let offset = 12;
  while (query[offset]) {
    const length = query[offset++];
    if (length > 63 || offset + length >= query.length) throw Error('Invalid DNS name');
    labels.push(new TextDecoder().decode(query.subarray(offset, offset + length))); offset += length;
  }
  offset++;
  if (offset + 4 > query.length) throw Error('Incomplete DNS question');
  const host = labels.join('.'), type = (query[offset] << 8) | query[offset + 1], kind = (query[offset + 2] << 8) | query[offset + 3];
  if (!host || host.length > 253 || !/^[a-zA-Z0-9._-]+$/.test(host)) throw Error('Invalid DNS hostname');
  const answer = type === 1 && kind === 1 ? dns.lookup_name(host).split('.').map(Number) : null;
  if (answer && (answer.length !== 4 || answer.some(value => !Number.isInteger(value) || value < 0 || value > 255))) throw Error('Invalid DNS mapping');
  const response = new Uint8Array(offset + 4 + (answer ? 16 : 0));
  response.set(query.subarray(0, offset + 4)); response[2] = 0x80 | (query[2] & 1); response[3] = 0x80;
  response[6] = 0; response[7] = answer ? 1 : 0; response.fill(0, 8, 12);
  if (answer) response.set([0xc0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, ...answer], offset + 4);
  return response;
}

export function createDnsSocket(dns) {
  let pending = new Uint8Array();
  const socket = {
    CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3, readyState: 0, bufferedAmount: 0,
    onopen: null, onmessage: null, onclose: null, onerror: null,
    close() { if (this.readyState === 3) return; this.readyState = 3; pending = new Uint8Array(); this.onclose?.({}); },
    send(bytes) {
      if (this.readyState !== 1) throw Error('DNS socket is closed');
      const data = ArrayBuffer.isView(bytes) ? new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength) : new Uint8Array(bytes);
      if (pending.length + data.length > 65536) { this.close(); throw Error('DNS message is too large'); }
      const next = new Uint8Array(pending.length + data.length); next.set(pending); next.set(data, pending.length); pending = next;
      while (pending.length >= 2) {
        const length = (pending[0] << 8) | pending[1];
        if (length < 12 || length > 4096) { this.close(); throw Error('Invalid DNS message size'); }
        if (pending.length < length + 2) return;
        let response;
        try { response = dnsResponse(pending.subarray(2, length + 2), dns); }
        catch (error) { this.onerror?.({ message: error.message }); this.close(); return; }
        pending = pending.slice(length + 2);
        const packet = new Uint8Array(response.length + 2); packet[0] = response.length >> 8; packet[1] = response.length & 255; packet.set(response, 2);
        queueMicrotask(() => { if (this.readyState === 1) this.onmessage?.({ data: packet.buffer }); });
      }
    },
  };
  queueMicrotask(() => { if (socket.readyState === 0) { socket.readyState = 1; socket.onopen?.({}); } });
  return socket;
}

export function createWispBridge(ClientConnection, { endpoint = wispEndpoint, timeout = 15000, onStatus = value => {} } = {}) {
  const address = new URL(endpoint);
  if (!['ws:', 'wss:'].includes(address.protocol) || address.username || address.password || address.hash) throw Error('Invalid Wisp endpoint');
  const sockets = new Set();
  let connection = null, timer = null, destroyed = false;
  function finish(socket, error = null) {
    if (socket.readyState === 3) return;
    socket.readyState = 3; sockets.delete(socket);
    if (error) socket.onerror?.({ message: error });
    socket.onclose?.({ code: error ? 1006 : 1000 });
  }
  function open(socket) {
    if (socket.readyState !== 0) return;
    try {
      const stream = connection.create_stream(socket.host, socket.port, 'tcp');
      socket.stream = stream; socket.readyState = 1;
      stream.onmessage = bytes => socket.onmessage?.({ data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) });
      stream.onclose = reason => finish(socket, reason > 2 ? 'The remote connection closed (' + reason + ')' : null);
      socket.onopen?.({});
    } catch (error) { finish(socket, error.message); }
  }
  function ensureConnection() {
    if (connection) return;
    connection = new ClientConnection(address.href);
    timer = setTimeout(() => {
      onStatus('Internet connection timed out. Check the Wisp server or restart the program.');
      for (const socket of sockets) finish(socket, 'Wisp connection timed out');
      connection?.close(); connection = null;
    }, timeout);
    connection.onopen = () => {
      clearTimeout(timer); onStatus('Internet connected via Wisp.');
      for (const socket of sockets) open(socket);
    };
    const disconnected = () => {
      clearTimeout(timer); connection = null;
      for (const socket of sockets) finish(socket, 'Wisp disconnected');
      if (!destroyed) onStatus('Internet disconnected. Restart the program to reconnect.');
    };
    connection.onclose = disconnected; connection.onerror = disconnected;
  }
  return {
    connect(host, port) {
      if (destroyed || typeof host !== 'string' || !/^[a-zA-Z0-9._-]+$/.test(host) || !Number.isInteger(port) || port < 1 || port > 65535) throw Error('Invalid network destination');
      const socket = {
        host, port, stream: null, readyState: 0, binaryType: 'arraybuffer',
        CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3,
        onopen: null, onmessage: null, onerror: null, onclose: null,
        get bufferedAmount() { return this.stream?.send_buffer.reduce((total, bytes) => total + bytes.byteLength, 0) || 0; },
        send(bytes) {
          if (this.readyState !== 1) throw Error('Network socket is not open');
          const data = ArrayBuffer.isView(bytes) ? new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength) : new Uint8Array(bytes);
          if (this.bufferedAmount + data.byteLength > 8 * 1024 * 1024) { this.close(); throw Error('Network send buffer exceeded'); }
          this.stream.send(data);
        },
        close() { try { this.stream?.close(); } finally { finish(this); } },
      };
      sockets.add(socket);
      try { ensureConnection(); if (connection.connected) queueMicrotask(() => open(socket)); }
      catch (error) { queueMicrotask(() => finish(socket, error.message)); }
      return socket;
    },
    destroy() {
      destroyed = true; clearTimeout(timer);
      for (const socket of sockets) socket.close();
      connection?.close(); connection = null;
    },
  };
}

export function installWispSockets(sockfs, dns, bridge) {
  const operations = sockfs.websocket_sock_ops, original = operations.createPeer;
  operations.createPeer = function (socket, address, port) {
    if (socket.type !== 1) throw Error('Internet access supports outbound TCP connections');
    const resolver = port === 53 && ['1.1.1.1', '8.8.8.8'].includes(address);
    const peer = { addr: address, port, socket: resolver ? createDnsSocket(dns) : bridge.connect(dns.lookup_addr?.(address) || address, port), msg_send_queue: [] };
    operations.addPeer(socket, peer); operations.handlePeerEvents(socket, peer);
    return peer;
  };
  return () => { operations.createPeer = original; bridge.destroy(); };
}
