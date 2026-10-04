import taskCatalog from "./tasks.json";
import achievementCatalog from "./achievements.json";

export const HEARTBEAT_MS = 15_000;
export const LEASE_MS = 30_000;
const MAX = 99_999_999;
export const ATTRS = ["speed", "endurance", "strength", "will", "intellect"] as const;
type Attribute = typeof ATTRS[number];
type Numbers = Record<string, number>;
export interface TaskDefinition {
  id: number; cost: number; title: string; desc: string;
  requirements: Numbers; rewards?: Numbers; repeatable: boolean;
  special?: { title: string; desc: string; linked_key: string };
}
export const TASKS = taskCatalog as TaskDefinition[];
export interface AchievementState {
  version: number; metrics: Numbers; unlocked: Numbers; dates: string[];
  streak: number; last_failed: boolean;
}
interface RunningTask { id: number; durationMs: number; elapsedMs: number; queued: boolean; startedAt: number }
export interface GameState {
  schema: number; uid: string; name: string; revision: number; share: boolean;
  attributes: Numbers; stars: number; clothes: { current: number; unlock: boolean[] };
  buffs: string[]; medal: number; failcount: number; achievements: AchievementState;
  archived: number[]; current: RunningTask | null;
  queue: { entry_id: number; task_id: number }[]; nextEntry: number;
  history: Record<string, unknown>[]; expProgressMs: number;
  session: string; leaseUntil: number; clockAt: number;
  touchTotal: number;
}
export type Command = { type: string; attr?: string; id?: number; entry_id?: number; direction?: number; count?: number; enabled?: boolean };
export class GameError extends Error {
  constructor(message: string, public code = "INVALID_ACTION", public status = 409) { super(message); }
}
export function number(value: unknown, fallback = 0, max = MAX): number {
  return typeof value === "number" && Number.isFinite(value) ? Math.min(max, Math.max(0, Math.floor(value))) : fallback;
}
function object(value: unknown): Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : {};
}
function achievementState(value: unknown): AchievementState {
  const source = object(value);
  const numbers = (value: unknown) => Object.fromEntries(Object.entries(object(value)).map(([key, v]) => [key, number(v, 0, Number.MAX_SAFE_INTEGER)]));
  return { version: 1, metrics: numbers(source.metrics), unlocked: numbers(source.unlocked),
    dates: Array.isArray(source.dates) ? [...new Set(source.dates.filter((d): d is string => typeof d === "string" && /^\d{4}-\d{2}-\d{2}$/.test(d)))].slice(0, 30) : [],
    streak: number(source.streak), last_failed: source.last_failed === true };
}
export function createGame(uid: string, name: string, now: number, bootstrap?: unknown): GameState {
  const source = object(bootstrap), profile = object(source.profile);
  const attributes: Numbers = { speed: 2, endurance: 1, strength: 1, will: 3, intellect: 4, exp: 0, buycnt: 0 };
  for (const key of Object.keys(attributes)) attributes[key] = number(object(profile.attributes)[key], attributes[key]);
  const clothes = object(profile.clothes);
  const unlock = [true, Array.isArray(clothes.unlock) && clothes.unlock[1] === true, Array.isArray(clothes.unlock) && clothes.unlock[2] === true];
  const currentClothes = number(clothes.current, 0, 2);
  const state: GameState = { schema: 1, uid, name, revision: 0, share: false, attributes,
    stars: number(profile.starcnt), clothes: { current: unlock[currentClothes] ? currentClothes : 0, unlock },
    buffs: [], medal: 0, failcount: number(source.failcount), achievements: achievementState(source.achievements),
    archived: [], current: null, queue: [], nextEntry: 1, history: [], expProgressMs: 0,
    session: "", leaseUntil: 0, clockAt: now, touchTotal: 0 };
  for (const attr of ATTRS) addAttribute(state, attr, 0);
  const legacyTasks = Array.isArray(source.tasks) ? source.tasks.map(object) : [];
  const legacyCompleted = new Set<number>();
  for (const record of legacyTasks) {
    const task = TASKS.find(t => t.id === record.id);
    if (!task) continue;
    if (record.status === 3 && !task.repeatable) state.archived.push(task.id);
    if (record.status === 1 && !state.current) {
      const durationMs = (number(record.cost_snapshot) || taskCost(state, task)) * 1000;
      state.current = { id: task.id, durationMs, elapsedMs: Math.min(durationMs, number(record.elapsed_seconds) * 1000),
        queued: record.queued === true, startedAt: now };
    }
    if (record.status === 2) settle(state, task, record.success === true, record.queued === true, now);
    // Old saves may have no achievement counters. Import only surviving evidence.
    if (source.achievements === undefined && (record.status === 3 || (record.status === 0 && record.success === true && number(record.end_time) > 0))) {
      legacyCompleted.add(task.id);
    }
  }
  const legacyQueue = Array.isArray(source.queue) ? source.queue.map(object) : [];
  for (const record of legacyQueue.slice(0, capacity(state))) {
    const id = number(record.task_id);
    const task = TASKS.find(t => t.id === id);
    if (!task || state.archived.includes(id) || (!task.repeatable && (state.current?.id === id || state.queue.some(e => e.task_id === id)))) continue;
    state.queue.push({ entry_id: state.nextEntry++, task_id: id });
  }
  const legacyHistory = Array.isArray(source.history) ? source.history.map(object).slice(0, 10) : [];
  state.history = [...state.history, ...legacyHistory].slice(0, 10);
  if (source.achievements === undefined) {
    for (const record of legacyHistory) {
      const id = number(record.id);
      if (!TASKS.some(t => t.id === id)) continue;
      if (record.success === true) increment(state.achievements, `task.${id}`);
      else increment(state.achievements, "failures");
    }
    for (const id of legacyCompleted) if (!state.achievements.metrics[`task.${id}`]) state.achievements.metrics[`task.${id}`] = 1;
    state.achievements.metrics.successes = TASKS.reduce((n, t) => n + (state.achievements.metrics[`task.${t.id}`] || 0), 0);
  }
  observe(state, now);
  return state;
}
export function capacity(state: GameState): number { return 2 + state.stars; }
function easeOut(x: number): number { const p = Math.min(100, Math.max(0, x)) / 100; return 1 - (1 - p) ** 2; }
export function expDiff(state: GameState): number {
  let exp = 1 + Math.ceil(499 * easeOut(state.attributes.intellect + Math.floor(state.medal / 3) - 4));
  for (const [buff, factor] of [["live", 2], ["dynamic", 1.5], ["guard", 1.25], ["monday", 1.25], ["birthday", 3.5]] as const) {
    if (state.buffs.includes(buff)) exp *= factor;
  }
  if (state.failcount >= 2) exp *= 1.25;
  return Math.min(MAX, Math.floor(exp * (1 + 0.1 * state.stars)));
}
export function taskCost(state: GameState, task: TaskDefinition): number {
  return Math.max(1, Math.floor(task.cost * (1 - 0.75 * easeOut(state.attributes.speed - 2))));
}
export function successRate(state: GameState, task: TaskDefinition): number {
  let lack = Object.entries(task.requirements).reduce((n, [key, value]) => n + Math.max(0, value - state.attributes[key]), 0);
  lack = lack * 12 + 120 + 20 * state.stars;
  return lack >= 400 ? 0 : (400 - Math.max(20, lack - state.attributes.will)) / 400;
}
function addAttribute(state: GameState, key: string, delta: number): void {
  const next = Math.min(MAX, Math.max(0, state.attributes[key] + delta));
  if ((ATTRS as readonly string[]).includes(key)) {
    const limit = 100 + state.stars * 10;
    if (next > limit) addAttribute(state, "exp", (next - limit) * 26500);
    state.attributes[key] = Math.min(limit, next);
  } else state.attributes[key] = next;
}
function increment(state: AchievementState, key: string, count = 1): void {
  state.metrics[key] = Math.min(999_999_999, (state.metrics[key] || 0) + count);
}
export function recordTouches(state: GameState, count: number, now: number): void {
  increment(state.achievements, "touches", number(count, 0, 1000));
  observe(state, now);
}
export function observe(state: GameState, now: number): void {
  const a = state.achievements, peak = (key: string, value: number) => { a.metrics[key] = Math.max(a.metrics[key] || 0, value); };
  for (const key of ATTRS) peak(key, state.attributes[key]);
  peak("balanced", Math.min(...ATTRS.map(k => state.attributes[k])));
  peak("exp", state.attributes.exp); peak("stars", state.stars);
  peak("dress", +state.clothes.unlock[1]); peak("winter", +state.clothes.unlock[2]);
  peak("clothes", state.clothes.unlock.filter(Boolean).length);
  peak("variety", TASKS.filter(t => (a.metrics[`task.${t.id}`] || 0) > 0).length);
  for (const d of achievementCatalog) {
    if ((a.metrics[d.metric] || 0) >= d.target && !a.unlocked[d.id]) a.unlocked[d.id] = Math.floor(now / 1000);
  }
}
function describeTask(state: GameState, task: TaskDefinition) {
  return { ...task, rewards: task.id === 1 ? { exp: 10 * expDiff(state) } : task.rewards || {},
    cost: taskCost(state, task), rate: successRate(state, task) * 100,
    status: state.archived.includes(task.id) ? 3 : 0, start_time: 0, end_time: 0, success: false };
}
function startNext(state: GameState, now: number): void {
  if (state.current || !state.queue.length) return;
  const entry = state.queue[0], task = TASKS.find(t => t.id === entry.task_id)!;
  if (successRate(state, task) === 0) return;
  state.queue.shift();
  state.current = { id: task.id, durationMs: taskCost(state, task) * 1000, elapsedMs: 0, queued: true, startedAt: now };
}
export function resumeGame(state: GameState, now: number): void { startNext(state, now); }
function settle(state: GameState, task: TaskDefinition, success: boolean, queued: boolean, now: number): void {
  const previous = { ...state.attributes }, a = state.achievements;
  if (success) {
    for (const [key, value] of Object.entries(task.rewards || {})) addAttribute(state, key, value);
    if (task.id === 1) addAttribute(state, "exp", 10 * expDiff(state));
    if (task.special) state.clothes.unlock[Number(task.special.linked_key.split(".")[1])] = true;
    state.failcount = 0;
    if (!task.repeatable && !state.archived.includes(task.id)) state.archived.push(task.id);
    increment(a, "successes"); increment(a, `task.${task.id}`);
    if (queued) increment(a, "queued_successes");
    if (a.last_failed) increment(a, "recoveries");
    a.streak++; a.metrics.best_streak = Math.max(a.metrics.best_streak || 0, a.streak);
  } else { state.failcount++; increment(a, "failures"); a.streak = 0; }
  a.last_failed = !success;
  const rewards = Object.fromEntries(Object.entries(state.attributes).filter(([key, value]) => value > previous[key]).map(([key, value]) => [key, value - previous[key]]));
  const record: Record<string, unknown> = { ...describeTask(state, task), success, rewards, end_time: Math.floor(now / 1000) };
  if (!success) delete record.special;
  state.history.unshift(record); state.history = state.history.slice(0, 10);
  observe(state, now);
}
// Advance only within the last server-issued online lease. Pauses retain fractions.
export function advance(state: GameState, now: number, random = () => crypto.getRandomValues(new Uint32Array(1))[0] / 2 ** 32): void {
  let cursor = state.clockAt;
  const end = Math.min(now, state.leaseUntil);
  if (cursor < end) startNext(state, cursor);
  while (cursor < end) {
    const task = state.current;
    const delta = Math.min(end - cursor, 60_000 - state.expProgressMs, task ? task.durationMs - task.elapsedMs : Infinity);
    state.expProgressMs += delta;
    if (task) task.elapsedMs += delta;
    cursor += delta;
    if (state.expProgressMs >= 60_000) {
      state.expProgressMs -= 60_000;
      addAttribute(state, "exp", expDiff(state)); increment(state.achievements, "minutes");
      const date = new Date(cursor + 8 * 3600_000).toISOString().slice(0, 10);
      if (state.achievements.dates.length < 30 && !state.achievements.dates.includes(date)) state.achievements.dates.push(date);
      state.achievements.metrics.days = Math.max(state.achievements.metrics.days || 0, state.achievements.dates.length);
      observe(state, cursor);
    }
    if (task && task.elapsedMs >= task.durationMs) {
      const definition = TASKS.find(t => t.id === task.id)!;
      const roll = Math.floor(random() * 400);
      settle(state, definition, roll >= Math.round((1 - successRate(state, definition)) * 400), task.queued, cursor);
      state.current = null;
      // A queue cannot start once the lease has expired.
      if (cursor < state.leaseUntil) startNext(state, cursor);
    }
  }
  state.clockAt = Math.max(state.clockAt, now);
}
export function updateBuffs(state: GameState, buffs: unknown, medal: unknown): void {
  const accepted = ["live", "dynamic", "guard", "monday", "birthday"];
  state.buffs = Array.isArray(buffs) ? accepted.filter(b => buffs.includes(b)) : [];
  state.medal = number(medal, 0, 1000);
}
export function command(state: GameState, action: Command, now: number): void {
  const task = TASKS.find(t => t.id === action.id);
  switch (action.type) {
    case "star":
      if (!ATTRS.every(k => state.attributes[k] >= 53)) throw new GameError("五项属性均需达到 53 才能升星");
      observe(state, now); for (const key of ATTRS) addAttribute(state, key, -53); state.stars++; break;
    case "attr.buy": case "attr.refund": {
      if (!(ATTRS as readonly string[]).includes(action.attr || "")) throw new GameError("无效属性");
      const attr = action.attr as Attribute, buys = state.attributes.buycnt;
      if (action.type === "attr.buy") {
        const cost = buyCost(state);
        if (state.attributes[attr] >= 100 + state.stars * 10) throw new GameError("属性已达上限");
        if (state.attributes.exp < cost) throw new GameError("经验不足");
        addAttribute(state, attr, 1); addAttribute(state, "exp", -cost); addAttribute(state, "buycnt", 1);
      } else {
        if (state.attributes[attr] <= 0 || buys <= 0) throw new GameError("没有可返还的属性点");
        addAttribute(state, attr, -1); addAttribute(state, "exp", refundGain(state)); addAttribute(state, "buycnt", -1);
      }
      break;
    }
    case "clothes":
      if (!Number.isInteger(action.id) || !state.clothes.unlock[action.id!]) throw new GameError("衣装尚未解锁");
      state.clothes.current = action.id!; break;
    case "task.start": case "task.queue":
      if (!task) throw new GameError("任务不存在");
      if (state.archived.includes(task.id)) throw new GameError("该任务已经完成");
      if (!task.repeatable && (state.current?.id === task.id || state.queue.some(e => e.task_id === task.id))) throw new GameError("该任务正在执行或排队");
      if (action.type === "task.start") {
        if (state.current || state.queue.length) throw new GameError("已有任务在运行或排队，请加入队列");
        if (successRate(state, task) === 0) throw new GameError("任务成功率为 0，请先提升属性");
        state.current = { id: task.id, durationMs: taskCost(state, task) * 1000, elapsedMs: 0, queued: false, startedAt: now };
      } else {
        if (state.queue.length >= capacity(state)) throw new GameError("任务队列已满，升星可增加容量");
        state.queue.push({ entry_id: state.nextEntry++, task_id: task.id });
      }
      break;
    case "task.cancel":
      if (!state.current || state.current.id !== action.id) throw new GameError("该任务当前未在运行");
      state.current = null; break;
    case "queue.remove": case "queue.move": {
      const index = state.queue.findIndex(e => e.entry_id === action.entry_id);
      if (index < 0) throw new GameError("该排队任务已开始或已被移除");
      if (action.type === "queue.remove") state.queue.splice(index, 1);
      else {
        if (action.direction !== -1 && action.direction !== 1) throw new GameError("无效的移动方向");
        const target = index + action.direction;
        if (target < 0 || target >= state.queue.length) throw new GameError("已到达队列边界");
        [state.queue[index], state.queue[target]] = [state.queue[target], state.queue[index]];
      }
      break;
    }
    case "touch": recordTouches(state, number(action.count, 1, 1000), now); break;
    case "share": state.share = action.enabled === true; break;
    case "reset": {
      const fresh = createGame(state.uid, state.name, now);
      const { session, leaseUntil, revision, share, buffs, medal, touchTotal } = state;
      Object.assign(state, fresh, { session, leaseUntil, revision, share, buffs, medal, touchTotal });
      break;
    }
    default: throw new GameError("未知操作", "INVALID_ACTION", 400);
  }
  observe(state, now);
  startNext(state, now);
}
export function buyCost(state: GameState): number { return state.attributes.buycnt < 25 ? Math.ceil(10 * 1.41 ** state.attributes.buycnt) : 53000; }
export function refundGain(state: GameState): number { return Math.floor((state.attributes.buycnt < 26 ? Math.ceil(10 * 1.41 ** Math.max(0, state.attributes.buycnt - 1)) : 53000) / 2); }
export function snapshot(state: GameState, now: number) {
  const online = !!state.session && state.leaseUntil > now;
  const current = state.current && { ...describeTask(state, TASKS.find(t => t.id === state.current!.id)!),
    status: 1, cost: state.current.durationMs / 1000, elapsed_seconds: state.current.elapsedMs / 1000,
    remaining_seconds: (state.current.durationMs - state.current.elapsedMs) / 1000,
    start_time: Math.floor(state.current.startedAt / 1000), paused: !online };
  return { schema: 1, uid: state.uid, revision: state.revision, server_time: now, online,
    lease_remaining_ms: Math.max(0, state.leaseUntil - now), heartbeat_ms: HEARTBEAT_MS,
    profile: { attributes: state.attributes, starcnt: state.stars, clothes: state.clothes,
      expdiff: expDiff(state), buffs: [...state.buffs, ...(state.failcount >= 2 ? ["fail"] : [])],
      exp_progress_seconds: state.expProgressMs / 1000, buycost: buyCost(state), revertgain: refundGain(state),
      attr_limit: 100 + state.stars * 10, star_available: ATTRS.every(k => state.attributes[k] >= 53) },
    tasks: { current, list: TASKS.filter(t => t.id !== state.current?.id).map(t => describeTask(state, t)),
      queue: state.queue.map(e => ({ ...describeTask(state, TASKS.find(t => t.id === e.task_id)!), entry_id: e.entry_id })),
      queue_capacity: capacity(state), history: state.history,
      queue_blocked: !state.current && state.queue.length > 0 && successRate(state, TASKS.find(t => t.id === state.queue[0].task_id)!) === 0 },
    achievements: { total: achievementCatalog.length, unlocked: achievementCatalog.filter(d => state.achievements.unlocked[d.id]).length,
      list: achievementCatalog.map(d => ({ ...d, progress: state.achievements.unlocked[d.id] ? d.target : Math.min(d.target, state.achievements.metrics[d.metric] || 0),
        unlocked: !!state.achievements.unlocked[d.id], unlocked_at: state.achievements.unlocked[d.id] || 0 })) },
    save: { failcount: state.failcount, achievements: state.achievements }, share: state.share };
}
