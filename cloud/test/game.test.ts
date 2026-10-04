import { describe, expect, it } from "vitest";
import { advance, command, createGame, expDiff, LEASE_MS, resumeGame, snapshot, successRate, TASKS, taskCost, updateBuffs } from "../src/game";

const epoch = 1_800_000_000_000;
function game() {
  const s = createGame("123", "轴伊", epoch);
  s.session = "session-1234567890"; s.leaseUntil = epoch + LEASE_MS;
  return s;
}
function trained() { const s = game(); for (const key of ["speed", "endurance", "strength", "will", "intellect"]) s.attributes[key] = 50; return s; }
function online(s: ReturnType<typeof game>, ms: number, random = () => 0.99) {
  for (let remaining = ms; remaining > 0;) {
    const step = Math.min(15_000, remaining);
    advance(s, s.clockAt + step, random);
    s.leaseUntil = s.clockAt + LEASE_MS;
    remaining -= step;
  }
}

describe("server-owned progression", () => {
  it("keeps the 13 tasks and 50 achievements", () => {
    expect(TASKS.map(t => t.id)).toEqual(Array.from({ length: 13 }, (_, i) => i + 1));
    expect(snapshot(game(), epoch).achievements.total).toBe(50);
  });
  it("restores a legacy running task without a saved duration", () => {
    const s = createGame("123", "轴伊", epoch, {
      tasks: [{ id: 1, status: 1, cost_snapshot: 0, elapsed_seconds: 20 }],
    });
    expect(s.current).toMatchObject({ durationMs: 300_000, elapsedMs: 20_000 });
  });
  it("counts only leased time and keeps sub-minute progress after hours offline", () => {
    const s = game(); advance(s, epoch + 8 * 3600_000);
    expect(s.expProgressMs).toBe(30_000); expect(s.attributes.exp).toBe(0);
    s.leaseUntil = s.clockAt + LEASE_MS;
    advance(s, s.clockAt + 30_000);
    expect(s.expProgressMs).toBe(0); expect(s.attributes.exp).toBe(1);
    expect(s.achievements.metrics.minutes).toBe(1);
  });
  it("freezes task progress, resumes remaining duration, and never credits offline rewards", () => {
    const s = trained(); command(s, { type: "task.start", id: 1 }, epoch);
    const duration = s.current!.durationMs;
    advance(s, epoch + 86400_000);
    expect(s.current!.elapsedMs).toBe(30_000); expect(s.history).toHaveLength(0);
    s.leaseUntil = s.clockAt + LEASE_MS;
    online(s, duration - 30_000);
    expect(s.current).toBeNull(); expect(s.history).toHaveLength(1);
    const rewards = s.attributes.exp;
    advance(s, s.clockAt); expect(s.attributes.exp).toBe(rewards);
  });
  it("retains the existing experience formula and external modifiers", () => {
    const s = game(); expect(expDiff(s)).toBe(1);
    updateBuffs(s, ["live", "guard", "invalid"], 30);
    expect(expDiff(s)).toBe(Math.floor((1 + Math.ceil(499 * (1 - 0.9 ** 2))) * 2 * 1.25));
    expect(s.buffs).toEqual(["live", "guard"]);
  });
  it("applies a changed buff after settling the previous interval", () => {
    const s = game(); online(s, 60_000); expect(s.attributes.exp).toBe(1);
    updateBuffs(s, ["live"], 0); online(s, 60_000); expect(s.attributes.exp).toBe(3);
  });
  it("uses the existing speed and success rules", () => {
    const s = trained(); expect(taskCost(s, TASKS[5])).toBe(3260);
    expect(successRate(s, TASKS[5])).toBe(0.825);
    s.stars = 14; expect(successRate(s, TASKS[5])).toBe(0);
  });
  it("buys and refunds atomically and rejects invalid attributes", () => {
    const s = game(); s.attributes.exp = 100;
    command(s, { type: "attr.buy", attr: "speed" }, epoch);
    expect(s.attributes).toMatchObject({ speed: 3, exp: 90, buycnt: 1 });
    command(s, { type: "attr.refund", attr: "speed" }, epoch);
    expect(s.attributes).toMatchObject({ speed: 2, exp: 95, buycnt: 0 });
    expect(() => command(s, { type: "attr.buy", attr: "exp" }, epoch)).toThrow("无效属性");
    expect(() => command(s, { type: "attr.refund", attr: "speed" }, epoch)).toThrow("没有可返还");
  });
  it("rejects purchases without sufficient experience without changing attributes", () => {
    const s = game(); const before = { ...s.attributes };
    expect(() => command(s, { type: "attr.buy", attr: "intellect" }, epoch)).toThrow("经验不足");
    expect(s.attributes).toEqual(before);
  });
  it("keeps earned achievements through spending and stars", () => {
    const s = trained(); for (const k of ["speed", "endurance", "strength", "will", "intellect"]) s.attributes[k] = 53;
    command(s, { type: "star" }, epoch);
    expect(s.stars).toBe(1); expect(s.attributes.speed).toBe(0);
    expect(s.achievements.unlocked.balanced_50).toBe(epoch / 1000);
    expect(snapshot(s, epoch).tasks.queue_capacity).toBe(3);
  });
  it("enforces FIFO capacity, reorder and duplicate one-off tasks", () => {
    const s = trained(); command(s, { type: "task.start", id: 2 }, epoch);
    command(s, { type: "task.queue", id: 8 }, epoch);
    expect(() => command(s, { type: "task.queue", id: 8 }, epoch)).toThrow("正在执行或排队");
    command(s, { type: "task.queue", id: 6 }, epoch);
    expect(() => command(s, { type: "task.queue", id: 4 }, epoch)).toThrow("队列已满");
    command(s, { type: "queue.move", entry_id: 2, direction: -1 }, epoch);
    command(s, { type: "task.cancel", id: 2 }, epoch);
    expect(s.current!.id).toBe(6); expect(s.queue[0].task_id).toBe(8);
  });
  it("blocks an impossible queue head until its conditions change", () => {
    const s = game(); command(s, { type: "task.queue", id: 13 }, epoch);
    expect(snapshot(s, epoch).tasks.queue_blocked).toBe(true);
    for (const k of ["speed", "endurance", "strength", "will", "intellect"]) s.attributes[k] = 100;
    resumeGame(s, epoch); expect(s.current!.id).toBe(13);
  });
  it("settles successful rewards once and turns overflow into experience", () => {
    const s = trained(); for (const k of ["speed", "endurance", "strength", "will", "intellect"]) s.attributes[k] = 100;
    command(s, { type: "task.start", id: 2 }, epoch);
    online(s, s.current!.durationMs);
    expect(s.history[0].rewards).toMatchObject({ exp: 79500 });
    expect(s.attributes.speed).toBe(100); expect(s.achievements.metrics.successes).toBe(1);
  });
  it("settles guaranteed failures and does not leave the queue stuck running", () => {
    const s = trained(); command(s, { type: "task.start", id: 2 }, epoch); s.stars = 14;
    online(s, s.current!.durationMs);
    expect(s.current).toBeNull(); expect(s.history[0].success).toBe(false); expect(s.failcount).toBe(1);
  });
  it("unlocks clothes and archives completed one-off tasks", () => {
    const s = trained(); command(s, { type: "task.start", id: 8 }, epoch);
    online(s, s.current!.durationMs);
    expect(s.clothes.unlock[1]).toBe(true);
    command(s, { type: "clothes", id: 1 }, s.clockAt);
    expect(s.clothes.current).toBe(1); expect(s.achievements.unlocked.dress).toBeTruthy();
    expect(() => command(s, { type: "task.start", id: 8 }, s.clockAt)).toThrow("已经完成");
  });
  it("imports old progress and settles an old unclaimed reward once", () => {
    const s = createGame("123", "轴伊", epoch, { profile: { attributes: { speed: 10, strength: 10, endurance: 10 } },
      tasks: [{ id: 2, status: 2, success: true }, { id: 6, status: 1, cost_snapshot: 200, elapsed_seconds: 25 }],
      queue: [{ task_id: 4 }] });
    expect(s.attributes.speed).toBe(11); expect(s.current!.elapsedMs).toBe(25_000);
    expect(s.queue).toHaveLength(1);
    advance(s, epoch + 3600_000); expect(s.current!.elapsedMs).toBe(25_000);
  });
  it("resets gameplay but retains session identity and ranking consent", () => {
    const s = trained(); s.share = true;
    command(s, { type: "task.start", id: 2 }, epoch);
    command(s, { type: "reset" }, epoch);
    expect(s.current).toBeNull(); expect(s.attributes.speed).toBe(2); expect(s.share).toBe(true);
    expect(s.session).toBe("session-1234567890"); expect(snapshot(s, epoch).achievements.unlocked).toBe(0);
  });
});
