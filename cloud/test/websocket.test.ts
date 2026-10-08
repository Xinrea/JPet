import { env } from "cloudflare:workers";
import { runInDurableObject, runDurableObjectAlarm, evictDurableObject } from "cloudflare:test";
import { afterEach, beforeAll, describe, expect, it } from "vitest";
import worker from "../src/index";
import { type GameState, type snapshot } from "../src/game";
import migration from "../migrations/0001_rank.sql?raw";

type Frame = { type: string; request_id?: string; status?: number; code?: string; snapshot?: ReturnType<typeof snapshot> };
const clients: WebSocket[] = [];
beforeAll(async () => { await env.RANK.batch(migration.split(";").filter(sql => sql.trim()).map(sql => env.RANK.prepare(sql))); });
afterEach(() => { for (const ws of clients.splice(0)) ws.close(1000); });
function payload(uid: string) {
  return { uid, name: "玩家 " + uid, session_id: crypto.randomUUID(), request_id: crypto.randomUUID(), buffs: [], medal: 0 };
}
async function connect(uid: string) {
  const response = await worker.fetch(new Request(`https://jpet.test/v1/socket?uid=${uid}`, { headers: { Upgrade: "websocket" } }), env);
  expect(response.status).toBe(101);
  const ws = response.webSocket!;
  ws.accept(); clients.push(ws);
  const frames: Frame[] = [];
  const waiters: (() => void)[] = [];
  ws.addEventListener("message", event => { frames.push(JSON.parse(event.data as string)); waiters.splice(0).forEach(wake => wake()); });
  async function next(match: (frame: Frame) => boolean): Promise<Frame> {
    const deadline = Date.now() + 3000;
    while (true) {
      const index = frames.findIndex(match);
      if (index >= 0) return frames.splice(index, 1)[0];
      await new Promise<void>((resolve, reject) => {
        const timer = setTimeout(() => { waiters.splice(waiters.indexOf(wake), 1); reject(new Error("WebSocket frame timeout")); }, Math.max(0, deadline - Date.now()));
        const wake = () => { clearTimeout(timer); resolve(); };
        waiters.push(wake);
      });
    }
  }
  return { ws, next, async request(type: string, data: ReturnType<typeof payload> & Record<string, unknown>) {
    ws.send(JSON.stringify({ type, ...data }));
    return next(frame => frame.type === "response" && frame.request_id === data.request_id);
  } };
}
async function seed(uid: string, change: (state: GameState) => void) {
  await runInDurableObject(env.PLAYERS.getByName(uid), (_instance, ctx) => {
    const state = JSON.parse(ctx.storage.sql.exec<{ state: string }>("SELECT state FROM game WHERE id=1").one().state) as GameState;
    change(state); ctx.storage.sql.exec("UPDATE game SET state=? WHERE id=1", JSON.stringify(state));
  });
}

describe("Game WebSocket", () => {
  it("requires a WebSocket upgrade and valid player routing", async () => {
    expect((await worker.fetch(new Request("https://jpet.test/v1/socket?uid=50001"), env)).status).toBe(426);
    expect((await worker.fetch(new Request("https://jpet.test/v1/socket?uid=../bad", { headers: { Upgrade: "websocket" } }), env)).status).toBe(400);
  });
  it("opens, runs commands, and replays committed results across reconnects", async () => {
    const p = payload("50002");
    const first = await connect(p.uid);
    expect((await first.request("open", { ...p, bootstrap: { profile: { attributes: { exp: 100 } } } })).status).toBe(200);
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "attr.buy", attr: "speed" } };
    const result = await first.request("command", purchase);
    expect(result.snapshot!.profile.attributes).toMatchObject({ exp: 90, speed: 3, buycnt: 1 });
    first.ws.close(1000);
    const second = await connect(p.uid);
    // Recovery sends the exact uncertain command before opening the new socket session.
    const replay = await second.request("command", purchase);
    expect(replay.status).toBe(200); expect(replay.snapshot!.profile.attributes.exp).toBe(90);
    await second.request("open", { ...p, request_id: crypto.randomUUID() });
    expect((await second.request("command", { ...purchase, action: { type: "star" } })).code).toBe("REQUEST_CONFLICT");
    const closed = await second.request("close", { ...p, request_id: crypto.randomUUID() });
    expect(closed.snapshot!.online).toBe(false);
  });
  it("pushes task completion and minute experience from an alarm without a heartbeat", async () => {
    const p = payload("50003"), socket = await connect(p.uid);
    const opened = await socket.request("open", p);
    await seed(p.uid, s => {
      s.clockAt = Date.now() - 20; s.expProgressMs = 59990;
      s.current = { id: 1, durationMs: 10, elapsedMs: 0, queued: false, startedAt: s.clockAt };
    });
    await runDurableObjectAlarm(env.PLAYERS.getByName(p.uid));
    const push = await socket.next(frame => frame.type === "snapshot" && frame.snapshot!.revision > opened.snapshot!.revision);
    expect(push.snapshot!.tasks.current).toBeNull();
    expect(push.snapshot!.tasks.history).toHaveLength(1);
    expect(push.snapshot!.profile.attributes.exp).toBeGreaterThan(0);
  });
  it("restores socket identity after hibernation and rejects another UID", async () => {
    const p = payload("50004"), socket = await connect(p.uid);
    await socket.request("open", p);
    await evictDurableObject(env.PLAYERS.getByName(p.uid));
    expect((await socket.request("heartbeat", { ...p, request_id: crypto.randomUUID() })).status).toBe(200);
    expect((await socket.request("command", { ...p, uid: "50005", request_id: crypto.randomUUID(), action: { type: "share", enabled: true } })).status).toBe(400);
    expect((await socket.request("heartbeat", { ...p, session_id: crypto.randomUUID(), request_id: crypto.randomUUID() })).code).toBe("SESSION_REPLACED");
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      expect(ctx.getWebSockets()[0].deserializeAttachment()).toEqual({ uid: p.uid, session: p.session_id });
    });
  });
  it("pushes session replacement immediately and blocks commands from the old device", async () => {
    const first = payload("50006"), oldSocket = await connect(first.uid);
    await oldSocket.request("open", first);
    const second = payload(first.uid), newSocket = await connect(second.uid);
    expect((await newSocket.request("open", second)).code).toBe("SESSION_BUSY");
    expect((await newSocket.request("open", { ...second, request_id: crypto.randomUUID(), take_over: true })).status).toBe(200);
    expect((await oldSocket.next(frame => frame.type === "session")).code).toBe("SESSION_REPLACED");
    const response = await worker.fetch(new Request("https://jpet.test/v1/command", { method: "POST", body: JSON.stringify({ ...first, request_id: crypto.randomUUID(), action: { type: "share", enabled: true } }) }), env);
    expect(response.status).toBe(409);
  });
  it("rejects WebSocket reset and retains progress when replayed after hibernation", async () => {
    const p = payload("50011"), socket = await connect(p.uid);
    await socket.request("open", { ...p, share: true, bootstrap: {
      profile: { starcnt: 3, attributes: { exp: 888, speed: 60, endurance: 60, strength: 60, will: 60, intellect: 60 },
        clothes: { current: 1, unlock: [true, true, true] } },
    } });
    await socket.request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "queue.upgrade" } });
    await socket.request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "task.start", id: 2 } });
    const queued = await socket.request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "task.queue", id: 4 } });
    expect(queued.status).toBe(200);
    const before = queued.snapshot!;
    const reset = { ...p, request_id: crypto.randomUUID(), action: { type: "reset" } };
    for (let attempt = 0; attempt < 2; attempt++) {
      const rejected = await socket.request("command", reset);
      expect(rejected.status).toBe(400); expect(rejected.code).toBe("INVALID_ACTION");
      const after = rejected.snapshot!;
      expect(after.profile.attributes).toEqual(before.profile.attributes);
      expect(after.profile.starcnt).toBe(before.profile.starcnt);
      expect(after.profile.clothes).toEqual(before.profile.clothes);
      expect(after.tasks).toMatchObject({ queue: before.tasks.queue, queue_capacity: 3,
        queue_upgrade: before.tasks.queue_upgrade, current: { id: 2, start_time: before.tasks.current!.start_time } });
      expect(after.tasks.current!.elapsed_seconds).toBeGreaterThanOrEqual(before.tasks.current!.elapsed_seconds);
      expect(after.save).toEqual(before.save); expect(after.share).toBe(true);
      if (attempt === 0) await evictDurableObject(env.PLAYERS.getByName(p.uid));
    }
  });
  it("keeps the last lease after transport loss and pushes pause on expiry", async () => {
    const p = payload("50007"), socket = await connect(p.uid);
    await socket.request("open", p);
    const observer = await connect(p.uid);
    await observer.request("open", { ...p, request_id: crypto.randomUUID() });
    socket.ws.close(1000);
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      const state = JSON.parse(ctx.storage.sql.exec<{ state: string }>("SELECT state FROM game WHERE id=1").one().state) as GameState;
      expect(state.leaseUntil).toBeGreaterThan(Date.now());
    });
    await seed(p.uid, s => { s.leaseUntil = Date.now() - 1; });
    await runDurableObjectAlarm(env.PLAYERS.getByName(p.uid));
    expect((await observer.next(frame => frame.type === "snapshot" && frame.snapshot!.online === false)).snapshot!.online).toBe(false);
  });
  it("rejects binary frames and oversized JSON", async () => {
    for (const [uid, message, code] of [["50008", new Uint8Array([1]).buffer, 1003], ["50009", "x".repeat(129 * 1024), 1009]] as const) {
      const socket = await connect(uid);
      const closed = new Promise<number>(resolve => socket.ws.addEventListener("close", event => resolve(event.code)));
      socket.ws.send(message);
      expect(await closed).toBe(code);
    }
  });
  it("replays business failures and pushes changes made through the compatible HTTP API", async () => {
    const p = payload("50010"), socket = await connect(p.uid);
    await socket.request("open", p);
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "attr.buy", attr: "speed" } };
    expect((await socket.request("command", purchase)).status).toBe(409);
    await seed(p.uid, s => { s.attributes.exp = 100; });
    const replay = await socket.request("command", purchase);
    expect(replay.status).toBe(409); expect(replay.snapshot!.profile.attributes.exp).toBe(100);
    const response = await worker.fetch(new Request("https://jpet.test/v1/command", { method: "POST", body: JSON.stringify({ ...p, request_id: crypto.randomUUID(), action: { type: "share", enabled: true } }) }), env);
    expect(response.status).toBe(200);
    const pushed = await socket.next(frame => frame.type === "snapshot" && frame.snapshot!.revision > replay.snapshot!.revision);
    expect(pushed.snapshot!.share).toBe(true);
  });
});
