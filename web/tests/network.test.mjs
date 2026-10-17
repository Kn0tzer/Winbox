import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createServer as createTcpServer } from 'node:net';
import { client, server } from '@mercuryworkshop/wisp-js';
import { createWispBridge, installWispSockets, dnsResponse, createDnsSocket } from '../network.mjs';

test('Wisp rejects unsafe endpoints and destinations before opening a transport', () => {
  let created = 0;
  class Connection { constructor() { created++; } }
  for (const endpoint of ['https://example.test/', 'wss://user:pass@example.test/', 'wss://example.test/#token']) {
    assert.throws(() => createWispBridge(Connection, { endpoint }));
  }
  const bridge = createWispBridge(Connection);
  for (const [host, port] of [['example.test/path', 80], ['example.test', 0], ['example.test', 65536], ['', 80]]) assert.throws(() => bridge.connect(host, port));
  assert.equal(created, 0);
  bridge.destroy(); assert.throws(() => bridge.connect('example.test', 80));
});

test('real Wisp client and server relay TCP bytes and close without leaks', { timeout: 15000 }, async () => {
  const echo = createTcpServer(socket => socket.on('data', bytes => socket.write(bytes)));
  const relay = createServer();
  const previous = { ...server.options };
  Object.assign(server.options, { allow_loopback_ips: true, allow_private_ips: true, allow_direct_ip: true, hostname_whitelist: [/^127\.0\.0\.1$/], port_blacklist: [], wisp_motd: null });
  relay.on('upgrade', (request, socket, head) => server.routeRequest(request, socket, head));
  await new Promise(resolve => echo.listen(0, '127.0.0.1', resolve));
  await new Promise(resolve => relay.listen(0, '127.0.0.1', resolve));
  const bridge = createWispBridge(client.ClientConnection, { endpoint: `ws://127.0.0.1:${relay.address().port}/wisp/` });
  try {
    const socket = bridge.connect('127.0.0.1', echo.address().port);
    const received = new Promise((resolve, reject) => { socket.onmessage = event => resolve(new Uint8Array(event.data)); socket.onerror = reject; });
    await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
    const bytes = new Uint8Array([0, 11, 22, 33, 255]);
    socket.send(bytes.subarray(1, 4));
    assert.deepEqual(await received, new Uint8Array([11, 22, 33]));
    assert.equal(socket.readyState, socket.OPEN);
    socket.close(); socket.close(); assert.equal(socket.readyState, socket.CLOSED);
  } finally {
    bridge.destroy(); Object.assign(server.options, previous);
    await Promise.all([new Promise(resolve => relay.close(resolve)), new Promise(resolve => echo.close(resolve))]);
  }
});

test('SOCKFS routes peers through Wisp and retains destination information', () => {
  const calls = [], connections = [];
  const original = () => { throw Error('Direct host WebSocket must not be used'); };
  const sockfs = { websocket_sock_ops: { createPeer: original, addPeer: (socket, peer) => calls.push(peer), handlePeerEvents: () => {} } };
  const cleanup = installWispSockets(sockfs, { lookup_addr: () => 'example.test' }, { connect: (host, port) => { connections.push([host, port]); return {}; }, destroy() {} });
  const peer = sockfs.websocket_sock_ops.createPeer({ type: 1 }, '172.29.1.1', 443);
  assert.deepEqual(connections, [['example.test', 443]]);
  assert.equal(peer.addr, '172.29.1.1'); assert.equal(peer.port, 443); assert.equal(calls.length, 1);
  assert.throws(() => sockfs.websocket_sock_ops.createPeer({ type: 2 }, 'example.test', 53), /TCP/);
  cleanup(); assert.equal(sockfs.websocket_sock_ops.createPeer, original);
});

test('guest DNS returns a synthetic IPv4 address and safely declines IPv6', () => {
  const query = new Uint8Array([0x12,0x34,1,0,0,1,0,0,0,0,0,0,7,101,120,97,109,112,108,101,3,99,111,109,0,0,1,0,1]);
  const dns = { lookup_name: host => { assert.equal(host, 'example.com'); return '172.29.1.1'; } };
  const response = dnsResponse(query, dns);
  assert.equal(response[0], 0x12); assert.equal(response[1], 0x34); assert.equal(response[7], 1);
  assert.deepEqual(Array.from(response.slice(-4)), [172,29,1,1]);
  query[query.length - 3] = 28; assert.equal(dnsResponse(query, dns)[7], 0);
  query[12] = 0xc0; assert.throws(() => dnsResponse(query, dns));
});

test('DNS over TCP supports fragmented questions and pipelined replies', async () => {
  const query = new Uint8Array([0,29,0x12,0x34,1,0,0,1,0,0,0,0,0,0,7,101,120,97,109,112,108,101,3,99,111,109,0,0,1,0,1]);
  const socket = createDnsSocket({ lookup_name: () => '172.29.2.1' });
  await new Promise(resolve => { socket.onopen = resolve; });
  const responses = [], reply = new Promise(resolve => { socket.onmessage = event => { responses.push(new Uint8Array(event.data)); if (responses.length === 2) resolve(); }; });
  socket.send(query.subarray(0, 5)); socket.send(query.subarray(5)); socket.send(query);
  await reply; assert.equal(responses.length, 2); assert.equal(responses[0][9], 1); socket.close();
  assert.equal(socket.readyState, 3);
});
