// Fault-injection fixture for the real native WebSocket transport.
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';
const require = createRequire(new URL('../cloud/package.json', import.meta.url));
const { WebSocketServer, WebSocket } = require('ws');
const port = Number(process.argv[2]);
const state = {
  schema: 1, uid: '123', revision: 1, online: true, lease_remaining_ms: 30000, share: false,
  profile: {
    attributes: { speed: 2, endurance: 1, strength: 1, will: 3, intellect: 4, exp: 100, buycnt: 0 },
    starcnt: 0, clothes: { current: 0, unlock: [true, false, false] },
    expdiff: 2, buffs: ['live'], exp_progress_seconds: 10,
  },
  tasks: { current: null, queue: [], list: [], history: [], queue_capacity: 2, queue_blocked: false },
  achievements: { total: 50, unlocked: 0, list: [] },
  save: { failcount: 0, achievements: { metrics: {}, unlocked: {} } },
};
const commands = new Set();
const stats = { connections: 0, commands: 0, attempts: 0, closes: 0, heartbeats: 0 };
let session = '', lost = false, unavailable = false;
function snapshot() { state.revision++; state.server_time = Date.now(); return state; }
const server = createServer((req, res) => {
  if (req.url === '/push') {
    state.profile.attributes.exp = 95;
    const message = JSON.stringify({ type: 'snapshot', snapshot: snapshot() });
    for (const socket of sockets.clients) if (socket.readyState === WebSocket.OPEN) socket.send(message);
  }
  res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(stats));
});
const sockets = new WebSocketServer({ server, maxPayload: 128 * 1024 });
sockets.on('connection', (socket, request) => {
  assert.equal(request.url, '/v1/socket?uid=123'); stats.connections++;
  socket.on('message', data => {
    const payload = JSON.parse(data.toString());
    assert.equal(payload.uid, '123'); assert.equal('cookies' in payload, false);
    if (payload.type === 'open') { session = payload.session_id; state.online = true; }
    if (payload.type === 'heartbeat') { assert.equal(payload.session_id, session); stats.heartbeats++; }
    if (payload.type === 'command') {
      stats.attempts++;
      assert.equal(payload.session_id, session);
      if (payload.action.type === 'task.start') {
        socket.send(JSON.stringify({ type: 'response', request_id: payload.request_id, status: 409, error: '任务不存在' }));
        return;
      }
      if (!commands.has(payload.request_id)) {
        commands.add(payload.request_id); stats.commands++;
        if (payload.action.type === 'share') state.share = payload.action.enabled;
        else { state.profile.attributes.speed = 3; state.profile.attributes.exp = 90; state.profile.attributes.buycnt = 1; }
      }
      if (!lost) { lost = true; snapshot(); socket.terminate(); return; }
      if (payload.action.type === 'share' && !unavailable) {
        unavailable = true;
        socket.send(JSON.stringify({ type: 'response', request_id: payload.request_id, status: 503, error: 'temporary failure' }));
        return;
      }
    }
    if (payload.type === 'close') { stats.closes++; state.online = false; }
    socket.send(JSON.stringify({ type: 'response', request_id: payload.request_id, status: 200, snapshot: snapshot() }));
    if (payload.type === 'close') socket.close(1000);
  });
});
server.listen(port, '127.0.0.1', () => process.stdout.write('READY\n'));
