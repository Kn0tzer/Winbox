import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import { WebSocket } from 'ws';
import test from 'node:test';
import { attachLoopbackTcpRelay } from '../lib/loopback-tcp-relay.mjs';

test('loopback relay rejects other origins and target overrides before forwarding', {timeout:10000}, async () => {
let requests=0;
const http=createServer((req,res)=>{requests++;res.setHeader('Connection','close');res.end('LOCAL_NETWORK_OK');});
await new Promise(resolve=>http.listen(0,'127.0.0.1',resolve));
const origin='http://127.0.0.1:'+http.address().port;
const relay=attachLoopbackTcpRelay(http);
function rejected(url,origin){return new Promise((resolve,reject)=>{
 const ws=new WebSocket(url,'binary',{origin,handshakeTimeout:2000});ws.on('error',()=>{});
 ws.once('open',()=>{ws.terminate();reject(Error('Unauthorized client connected'));});
 ws.once('unexpected-response',(req,res)=>{const code=res.statusCode;ws.terminate();resolve(code);});
});}
try{
 // Abuse case first: an unrelated web origin must never reach the TCP target.
 assert.equal(await rejected(relay.url,'https://unrelated.invalid'),403);
 assert.equal(requests,0);assert.equal(relay.stats.accepted,0);
 assert.equal(await rejected(relay.url+'?target=169.254.169.254',origin),403);
 assert.equal(requests,0);
 const response=await new Promise((resolve,reject)=>{
  const ws=new WebSocket(relay.url,'binary',{origin});let data=Buffer.alloc(0);
  const timer=setTimeout(()=>{ws.terminate();reject(Error('Relay response timed out'));},3000);
  ws.on('error',reject);
  ws.once('open',()=>ws.send(Buffer.from('GET /fixture HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')));
  ws.on('message',chunk=>{data=Buffer.concat([data,chunk]);});
  ws.once('close',()=>{clearTimeout(timer);resolve(data.toString());});
 });
 assert.ok(response.includes('200 OK')&&response.endsWith('LOCAL_NETWORK_OK'),response);
 assert.equal(requests,1);assert.equal(relay.stats.accepted,1);
 assert.ok(relay.stats.toTcpBytes>0&&relay.stats.toWebSocketBytes>0);
 assert.equal(relay.stats.rejected,2);
}finally{await relay.close();await new Promise(resolve=>http.close(resolve));}

});

test('rejected upgrade cannot keep a half-open socket alive', { timeout: 5000 }, async () => {
  const { connect } = await import('node:net');
  const server = createServer((request, response) => response.end());
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const relay = attachLoopbackTcpRelay(server);
  const client = connect({ host: '127.0.0.1', port: server.address().port, allowHalfOpen: true });
  let response = '';
  client.on('data', data => { response += data.toString(); });
  client.on('error', () => {});
  try {
    await new Promise((resolve, reject) => {
      client.once('connect', resolve);
      client.once('error', reject);
    });
    client.write('GET /runtime/__bw64_tcp HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nOrigin: https://unrelated.invalid\r\n\r\n');
    await new Promise((resolve, reject) => {
      let check;
      const deadline = setTimeout(() => { clearInterval(check); reject(Error('Rejected socket survived its deadline')); }, 2500);
      check = setInterval(() => server.getConnections((error, count) => {
        if (error) { clearInterval(check); clearTimeout(deadline); reject(error); }
        else if (response.includes('403 Forbidden') && count === 0) {
          clearInterval(check); clearTimeout(deadline); resolve();
        }
      }), 25);
    });
    assert.equal(relay.stats.rejected, 1);
    assert.equal(relay.stats.accepted, 0);
  } finally {
    client.destroy();
    await relay.close();
    await new Promise(resolve => server.close(resolve));
  }
});
