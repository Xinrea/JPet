import { env } from "cloudflare:workers";
import { runInDurableObject, runDurableObjectAlarm, evictDurableObject } from "cloudflare:test";
import { beforeAll, describe, expect, it } from "vitest";
import worker from "../src/index";
import type { GameState } from "../src/game";
import migration from "../migrations/0001_rank.sql?raw";

beforeAll(async () => { await env.RANK.batch(migration.split(";").filter(sql => sql.trim()).map(sql => env.RANK.prepare(sql))); });
function payload(uid = "10001") {
  return { uid, name: "玩家 " + uid, session_id: crypto.randomUUID(), request_id: crypto.randomUUID(), buffs: [], medal: 0 };
}
async function request(kind: string, data: object) {
  const response = await worker.fetch(new Request("https://jpet.test/v1/" + kind, { method: "POST", body: JSON.stringify(data) }), env);
  return { status: response.status, data: await response.json() as Record<string, any> };
}
async function seed(uid: string, change: (s: GameState) => void) {
  await runInDurableObject(env.PLAYERS.getByName(uid), (_instance, ctx) => {
    const state = JSON.parse(ctx.storage.sql.exec<{ state: string }>("SELECT state FROM game WHERE id=1").one().state) as GameState;
    change(state); ctx.storage.sql.exec("UPDATE game SET state=? WHERE id=1", JSON.stringify(state));
  });
}

describe("Worker and durable storage", () => {
  it("upgrades existing replay history and prunes it with bounded indexed reads", async () => {
    const p = payload("30001");
    await request("open", p);
    const now = Date.now();
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      ctx.storage.transactionSync(() => {
        ctx.storage.sql.exec("DELETE FROM requests");
        for (let i = 0; i < 4096; i++) {
          ctx.storage.sql.exec("INSERT INTO requests VALUES(?, ?, ?, ?)", `history-${i}`, "seed", '{"status":200,"body":{}}', now - 10000 + i);
        }
        ctx.storage.sql.exec("INSERT INTO requests VALUES('expired', 'seed', '{}', ?)", now - 8 * 86400_000);
        ctx.storage.sql.exec("DROP TABLE request_history");
        ctx.storage.sql.exec("DROP INDEX requests_created_at");
      });
    });
    await evictDurableObject(env.PLAYERS.getByName(p.uid));
    const beat = { ...p, request_id: crypto.randomUUID() };
    expect((await request("heartbeat", beat)).status).toBe(200);
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      expect(ctx.storage.sql.exec<{ count: number }>("SELECT count FROM request_history").one().count).toBe(4096);
      expect(ctx.storage.sql.exec<{ count: number }>("SELECT COUNT(*) AS count FROM requests").one().count).toBe(4096);
      expect(ctx.storage.sql.exec("SELECT id FROM requests WHERE id IN ('expired', 'history-0')").toArray()).toHaveLength(0);
      expect(ctx.storage.sql.exec("SELECT id FROM requests WHERE id=?", beat.request_id).toArray()).toHaveLength(1);
      const expiry = ctx.storage.sql.exec("DELETE FROM requests WHERE created_at < ? RETURNING id", now - 7 * 86400_000);
      expect(expiry.toArray()).toHaveLength(0);
      expect(expiry.rowsRead).toBeLessThan(10);
      const oldest = ctx.storage.sql.exec("SELECT id FROM requests ORDER BY created_at ASC, id ASC LIMIT 1");
      expect(oldest.toArray()).toHaveLength(1);
      expect(oldest.rowsRead).toBeLessThan(10);
      console.log(JSON.stringify({ cleanupExpiryRowsRead: expiry.rowsRead, cleanupOldestRowsRead: oldest.rowsRead }));
    });
    await evictDurableObject(env.PLAYERS.getByName(p.uid));
    expect((await request("heartbeat", beat)).status).toBe(200);
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      expect(ctx.storage.sql.exec<{ count: number }>("SELECT count FROM request_history").one().count).toBe(4096);
    });
  });
  it("opens isolated per-player saves and imports only on first creation", async () => {
    const p = payload("10001");
    const first = await request("open", { ...p, bootstrap: { profile: { attributes: { exp: 100 } } } });
    expect(first.status).toBe(200); expect(first.data.snapshot.profile.attributes.exp).toBe(100);
    await request("close", { ...p, request_id: crypto.randomUUID() });
    const second = await request("open", { ...p, request_id: crypto.randomUUID(), bootstrap: { profile: { attributes: { exp: 999 } } } });
    expect(second.data.snapshot.profile.attributes.exp).toBe(100);
    expect((await request("open", payload("10002"))).data.snapshot.profile.attributes.exp).toBe(0);
  });
  it("deduplicates purchases even after a response is lost", async () => {
    const p = payload("10003");
    await request("open", { ...p, bootstrap: { profile: { attributes: { exp: 100 } } } });
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "attr.buy", attr: "speed" } };
    const first = await request("command", purchase); const replay = await request("command", purchase);
    expect(first.status).toBe(200); expect(replay.data.snapshot.profile.attributes).toMatchObject({ exp: 90, speed: 3, buycnt: 1 });
    const conflicting = await request("command", { ...purchase, action: { type: "star" } });
    expect(conflicting.data.code).toBe("REQUEST_CONFLICT");
  });
  it("returns the same failed command on replay without converting it to success", async () => {
    const p = payload("10004"); await request("open", p);
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "attr.buy", attr: "speed" } };
    expect((await request("command", purchase)).status).toBe(409);
    await seed(p.uid, s => { s.attributes.exp = 100; });
    expect((await request("command", purchase)).status).toBe(409);
  });
  it("persists queue upgrades and deducts stars only once across retries, eviction and reconnects", async () => {
    const p = payload("10012");
    await request("open", { ...p, bootstrap: { profile: { starcnt: 3 } } });
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "queue.upgrade" } };
    const first = await request("command", purchase);
    expect(first.status).toBe(200);
    expect(first.data.snapshot.tasks).toMatchObject({ queue_capacity: 3, queue_upgrade: { cost: 2, stars: 2, available: true } });
    await evictDurableObject(env.PLAYERS.getByName(p.uid));
    const replay = await request("command", purchase);
    expect(replay.status).toBe(200); expect(replay.data.snapshot.profile.starcnt).toBe(2);
    expect(replay.data.snapshot.tasks.queue_capacity).toBe(3);
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      const saved = JSON.parse(ctx.storage.sql.exec<{ state: string }>("SELECT state FROM game WHERE id=1").one().state) as GameState;
      expect(saved.queueUpgrades).toBe(1); expect(saved.stars).toBe(2);
    });
    expect(await env.RANK.prepare("SELECT starcnt FROM rankboard WHERE uid=?").bind(p.uid).first("starcnt")).toBe(2);
    await request("close", { ...p, request_id: crypto.randomUUID() });
    const replacement = payload(p.uid);
    const reopened = await request("open", replacement);
    expect(reopened.data.snapshot.tasks.queue_capacity).toBe(3);
    const next = await request("command", { ...replacement, request_id: crypto.randomUUID(), action: { type: "queue.upgrade" } });
    expect(next.status).toBe(200);
    expect(next.data.snapshot.tasks).toMatchObject({ queue_capacity: 4, queue_upgrade: { cost: 5, stars: 0, available: false } });
  });
  it("replays an unaffordable queue upgrade as a failure after stars become available", async () => {
    const p = payload("10013"); await request("open", p);
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "queue.upgrade" } };
    const first = await request("command", purchase);
    expect(first.status).toBe(409); expect(first.data.error).toContain("星星不足");
    expect(first.data.snapshot.tasks.queue_capacity).toBe(2);
    await seed(p.uid, s => { s.stars = 1; });
    const replay = await request("command", purchase);
    expect(replay.status).toBe(409); expect(replay.data.snapshot.profile.starcnt).toBe(1);
    const next = await request("command", { ...purchase, request_id: crypto.randomUUID() });
    expect(next.status).toBe(200); expect(next.data.snapshot.profile.starcnt).toBe(0);
    expect(next.data.snapshot.tasks.queue_capacity).toBe(3);
  });
  it("migrates old cloud saves without spending stars or removing queued tasks", async () => {
    const p = payload("10014"); await request("open", p);
    await seed(p.uid, s => {
      s.schema = 1; s.stars = 3; delete (s as Partial<GameState>).queueUpgrades;
      s.current = { id: 1, durationMs: 300_000, elapsedMs: 0, queued: false, startedAt: Date.now() };
      s.queue = [2, 4, 6].map((task_id, i) => ({ entry_id: i + 1, task_id })); s.nextEntry = 4;
    });
    await evictDurableObject(env.PLAYERS.getByName(p.uid));
    const migrated = await request("heartbeat", { ...p, request_id: crypto.randomUUID() });
    expect(migrated.status).toBe(200); expect(migrated.data.snapshot.schema).toBe(1);
    expect(migrated.data.snapshot.profile.starcnt).toBe(3);
    expect(migrated.data.snapshot.tasks.queue_capacity).toBe(2);
    expect(migrated.data.snapshot.tasks.queue.map((e: { id: number }) => e.id)).toEqual([2, 4, 6]);
    await runInDurableObject(env.PLAYERS.getByName(p.uid), (_instance, ctx) => {
      const saved = JSON.parse(ctx.storage.sql.exec<{ state: string }>("SELECT state FROM game WHERE id=1").one().state) as GameState;
      expect(saved.schema).toBe(2); expect(saved.queueUpgrades).toBe(0);
    });
    const full = await request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "task.queue", id: 1 } });
    expect(full.status).toBe(409); expect(full.data.error).toContain("队列已满");
  });
  it("pauses at lease expiry, keeps fractions in SQLite, and resumes without backfill", async () => {
    const p = payload("10005"); await request("open", p);
    await seed(p.uid, s => { s.clockAt = Date.now() - 3600_000; s.leaseUntil = s.clockAt + 30_000; });
    await runDurableObjectAlarm(env.PLAYERS.getByName(p.uid));
    const resumed = await request("heartbeat", { ...p, request_id: crypto.randomUUID() });
    expect(resumed.data.snapshot.profile.exp_progress_seconds).toBeGreaterThanOrEqual(30);
    expect(resumed.data.snapshot.profile.exp_progress_seconds).toBeLessThan(31);
    expect(resumed.data.snapshot.profile.attributes.exp).toBe(0);
  });
  it("keeps stale devices from advancing a replaced session", async () => {
    const first = payload("10006"); await request("open", first);
    const second = payload(first.uid);
    expect((await request("open", second)).data.code).toBe("SESSION_BUSY");
    expect((await request("open", { ...second, take_over: true })).status).toBe(200);
    expect((await request("heartbeat", { ...first, request_id: crypto.randomUUID() })).data.code).toBe("SESSION_REPLACED");
    const closed = await request("close", { ...second, request_id: crypto.randomUUID() });
    expect(closed.data.snapshot.online).toBe(false);
  });
  it("counts client touches once across heartbeats and retries", async () => {
    const p = payload("10007"); await request("open", p);
    const beat = { ...p, request_id: crypto.randomUUID(), touch_total: 50 };
    const first = await request("heartbeat", beat); const second = await request("heartbeat", beat);
    expect(first.data.snapshot.save.achievements.metrics.touches).toBe(50);
    expect(second.data.snapshot.save.achievements.metrics.touches).toBe(50);
  });
  it("settles unsynced touches on commands and close without duplicate counting", async () => {
    const p = payload("10011"); await request("open", p);
    const purchase = { ...p, request_id: crypto.randomUUID(), touch_total: 7, action: { type: "attr.buy", attr: "speed" } };
    expect((await request("command", purchase)).data.snapshot.save.achievements.metrics.touches).toBe(7);
    expect((await request("command", purchase)).data.snapshot.save.achievements.metrics.touches).toBe(7);
    const closed = await request("close", { ...p, request_id: crypto.randomUUID(), touch_total: 10 });
    expect(closed.data.snapshot.save.achievements.metrics.touches).toBe(10);
    expect(closed.data.snapshot.online).toBe(false);
  });
  it("projects consented scores to D1 with stable tie ordering and personal rank", async () => {
    for (const uid of ["20001", "20002"]) await request("open", { ...payload(uid), share: true, bootstrap: { profile: { starcnt: 3, attributes: { exp: 77 } } } });
    await request("open", { ...payload("20003"), bootstrap: { profile: { starcnt: 9 } } });
    const response = await worker.fetch(new Request("https://jpet.test/v1/rank?metric=starcnt&uid=20002"), env);
    const result = await response.json() as { entries: { uid: string }[]; me: { rank: number } };
    expect(result.entries.map(e => e.uid)).toEqual(["20001", "20002"]); expect(result.me.rank).toBe(2);
  });
  it("preserves rank participation across heartbeats and new devices until explicitly changed", async () => {
    const p = payload("20004"); await request("open", p);
    await request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "share", enabled: true } });
    const beat = await request("heartbeat", { ...p, request_id: crypto.randomUUID(), share: false });
    expect(beat.data.snapshot.share).toBe(true);
    const replacement = { ...payload(p.uid), take_over: true, share: false };
    expect((await request("open", replacement)).data.snapshot.share).toBe(true);
    const withdrawn = await request("command", { ...replacement, request_id: crypto.randomUUID(), action: { type: "share", enabled: false } });
    expect(withdrawn.data.snapshot.share).toBe(false);
    expect(await env.RANK.prepare("SELECT visible FROM rankboard WHERE uid=?").bind(p.uid).first("visible")).toBe(0);
  });
  it("clears cloud state on reset and never re-imports a stale local bootstrap", async () => {
    const p = payload("10008"); await request("open", { ...p, bootstrap: { profile: { attributes: { exp: 888 } } } });
    const reset = await request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "reset" } });
    expect(reset.data.snapshot.profile.attributes.exp).toBe(0);
    const retry = await request("open", { ...p, request_id: crypto.randomUUID(), bootstrap: { profile: { attributes: { exp: 888 } } } });
    expect(retry.data.snapshot.profile.attributes.exp).toBe(0);
  });
  it("rejects malformed requests and oversized payloads", async () => {
    expect((await request("open", { ...payload(), uid: "../123" })).status).toBe(400);
    expect((await request("open", { ...payload(), bootstrap: "x".repeat(129 * 1024) })).status).toBe(413);
  });
  it("recovers the committed save and request replay after object eviction", async () => {
    const p = payload("10009");
    await request("open", { ...p, bootstrap: { profile: { attributes: { exp: 100 } } } });
    const purchase = { ...p, request_id: crypto.randomUUID(), action: { type: "attr.buy", attr: "speed" } };
    await request("command", purchase);
    await evictDurableObject(env.PLAYERS.getByName(p.uid));
    const replay = await request("command", purchase);
    expect(replay.data.snapshot.profile.attributes).toMatchObject({ speed: 3, exp: 90, buycnt: 1 });
  });
  it("keeps committed gameplay when D1 fails and retries the projection from an alarm", async () => {
    const p = payload("10010");
    await request("open", { ...p, bootstrap: { profile: { attributes: { exp: 100 } } } });
    await env.RANK.prepare("DROP TABLE rankboard").run();
    const purchase = await request("command", { ...p, request_id: crypto.randomUUID(), action: { type: "attr.buy", attr: "speed" } });
    expect(purchase.status).toBe(200); expect(purchase.data.snapshot.profile.attributes.exp).toBe(90);
    await env.RANK.batch(migration.split(";").filter(sql => sql.trim()).map(sql => env.RANK.prepare(sql)));
    await runDurableObjectAlarm(env.PLAYERS.getByName(p.uid));
    expect(await env.RANK.prepare("SELECT exp FROM rankboard WHERE uid=?").bind(p.uid).first("exp")).toBe(90);
  });
});
