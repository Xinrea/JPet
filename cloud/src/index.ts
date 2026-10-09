import { routeAI } from "./ai-auth";
export { AiAccount } from "./ai-service";
import { DurableObject } from "cloudflare:workers";
import { advance, command, createGame, GameError, LEASE_MS, migrateGame, number, recordTouches, resumeGame, snapshot, updateBuffs, type Command, type GameState } from "./game";

type Kind = "open" | "heartbeat" | "command" | "close";
interface PlayerRequest {
  uid: string; name: string; session_id: string; request_id: string;
  buffs?: unknown; medal?: unknown; bootstrap?: unknown; action?: Command; take_over?: boolean;
  share?: boolean; touch_total?: number;
}
interface Result { status: number; body: Record<string, unknown> }
interface SocketIdentity { uid: string; session: string }
const MAX_REQUEST_BYTES = 128 * 1024;

function validUid(uid: unknown): uid is string { return typeof uid === "string" && /^[1-9]\d{0,19}$/.test(uid); }
function validate(value: unknown): PlayerRequest {
  const p = value as PlayerRequest | null;
  if (!p || typeof p !== "object" || !validUid(p.uid) ||
    typeof p.name !== "string" || !p.name.trim() || p.name.length > 80 ||
    typeof p.session_id !== "string" || !/^[\w-]{16,80}$/.test(p.session_id) ||
    typeof p.request_id !== "string" || !/^[\w-]{16,80}$/.test(p.request_id)) throw new GameError("身份或会话格式无效", "BAD_REQUEST", 400);
  return p;
}

export class Player extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.storage.sql.exec(`CREATE TABLE IF NOT EXISTS game (id INTEGER PRIMARY KEY CHECK (id = 1), state TEXT NOT NULL, rank_dirty INTEGER NOT NULL DEFAULT 1);
      CREATE TABLE IF NOT EXISTS requests (id TEXT PRIMARY KEY, fingerprint TEXT NOT NULL, result TEXT NOT NULL, created_at INTEGER NOT NULL);
      CREATE INDEX IF NOT EXISTS requests_created_at ON requests(created_at, id);
      CREATE TABLE IF NOT EXISTS request_history (id INTEGER PRIMARY KEY CHECK (id = 1), count INTEGER NOT NULL);`);
    // Count existing history only once when upgrading an older object.
    ctx.storage.transactionSync(() => {
      if (!ctx.storage.sql.exec("SELECT count FROM request_history WHERE id=1").toArray().length) {
        ctx.storage.sql.exec("INSERT INTO request_history(id, count) SELECT 1, COUNT(*) FROM requests");
      }
    });
  }

  private read(): GameState | null {
    const rows = this.ctx.storage.sql.exec<{ state: string }>("SELECT state FROM game WHERE id = 1").toArray();
    return rows.length ? migrateGame(JSON.parse(rows[0].state) as GameState) : null;
  }
  private write(state: GameState): void {
    const previous = this.read();
    const projection = (s: GameState) => JSON.stringify([s.name, s.share, s.stars, s.attributes.exp,
      s.attributes.speed + s.attributes.endurance + s.attributes.strength + s.attributes.will + s.attributes.intellect]);
    const dirty = !previous || projection(previous) !== projection(state);
    state.revision++;
    this.ctx.storage.sql.exec("INSERT INTO game(id, state, rank_dirty) VALUES(1, ?, 1) ON CONFLICT(id) DO UPDATE SET state=excluded.state, rank_dirty=MAX(game.rank_dirty, ?)", JSON.stringify(state), +dirty);
  }

  async fetch(request: Request): Promise<Response> {
    const uid = new URL(request.url).searchParams.get("uid");
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket" || !validUid(uid)) return json({ error: "无效 WebSocket 请求" }, 400);
    const [client, server] = Object.values(new WebSocketPair());
    server.serializeAttachment({ uid, session: "" } satisfies SocketIdentity);
    this.ctx.acceptWebSocket(server);
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    let requestId = "";
    try {
      if (typeof message !== "string") { ws.close(1003, "JSON text required"); return; }
      if (new TextEncoder().encode(message).length > MAX_REQUEST_BYTES) { ws.close(1009, "Message too large"); return; }
      let envelope: PlayerRequest & { type: Kind };
      try { envelope = JSON.parse(message); } catch { throw new GameError("无效 JSON", "BAD_REQUEST", 400); }
      const payload = validate(envelope);
      requestId = payload.request_id;
      const kind = envelope.type;
      if (!["open", "heartbeat", "command", "close"].includes(kind)) throw new GameError("无效消息类型", "BAD_REQUEST", 400);
      if (kind === "command" && (!payload.action || typeof payload.action !== "object" || typeof payload.action.type !== "string")) throw new GameError("缺少操作指令", "BAD_REQUEST", 400);
      const identity = ws.deserializeAttachment() as SocketIdentity;
      if (payload.uid !== identity.uid) throw new GameError("连接账号不匹配", "BAD_REQUEST", 400);
      if (identity.session && identity.session !== payload.session_id) throw new GameError("连接会话不匹配", "SESSION_REPLACED", 409);
      const result = await this.execute(kind, payload);
      if (kind === "open" && result.status === 200) {
        // Attachments survive hibernation. Only an opened session receives pushes.
        identity.session = payload.session_id;
        ws.serializeAttachment(identity);
        for (const other of this.ctx.getWebSockets()) {
          if (other !== ws && (other.deserializeAttachment() as SocketIdentity).session === identity.session) other.close(1000, "Reconnected");
        }
      }
      ws.send(JSON.stringify({ type: "response", request_id: requestId, status: result.status, ...result.body }));
      const acknowledged = result.body.snapshot as ReturnType<typeof snapshot> | undefined;
      if (acknowledged) this.broadcast(ws, acknowledged.revision);
      if (kind === "close" && result.status === 200) ws.close(1000, "Session closed");
    } catch (error) {
      const known = error instanceof GameError;
      if (!known) console.error(JSON.stringify({ message: "WebSocket request failed", error: String(error) }));
      try { ws.send(JSON.stringify({ type: "response", request_id: requestId, status: known ? error.status : 503,
        error: known ? error.message : "云端服务暂时不可用，请稍后重试", code: known ? error.code : "SERVER_ERROR" })); } catch { /* Disconnected; request can be replayed. */ }
    }
  }

  webSocketClose(ws: WebSocket, code: number): void {
    // An abrupt transport loss keeps the last lease, allowing uncertain commands
    // to be replayed on reconnect. A normal client exit sends an explicit close.
    ws.close(code === 1005 || code === 1006 ? 1000 : code);
  }
  webSocketError(ws: WebSocket): void { ws.close(1011, "Connection error"); }

  private broadcast(source?: WebSocket, acknowledgedRevision?: number): void {
    const state = this.read();
    if (!state) return;
    const message = JSON.stringify({ type: "snapshot", snapshot: snapshot(state, Date.now()) });
    for (const ws of this.ctx.getWebSockets()) {
      const identity = ws.deserializeAttachment() as SocketIdentity;
      if (!identity.session) continue;
      if (ws === source && state.revision === acknowledgedRevision && identity.session === state.session) continue;
      try {
        if (state.session && identity.session !== state.session) {
          ws.send(JSON.stringify({ type: "session", code: "SESSION_REPLACED", error: "会话已被其他设备接管" }));
          ws.close(4001, "Session replaced");
        } else ws.send(message);
      } catch { /* A dead socket must not prevent a committed game update. */ }
    }
  }

  async run(kind: Kind, payload: PlayerRequest): Promise<string> {
    const result = await this.execute(kind, payload);
    if (result.body.snapshot) this.broadcast();
    return JSON.stringify(result);
  }

  private async execute(kind: Kind, payload: PlayerRequest): Promise<Result> {
    const now = Date.now();
    const fingerprint = JSON.stringify([kind, payload.session_id, payload.action || null]);
    const result = this.ctx.storage.transactionSync((): Result => {
      const previous = this.ctx.storage.sql.exec<{ fingerprint: string; result: string }>("SELECT fingerprint, result FROM requests WHERE id = ?", payload.request_id).toArray()[0];
      let state = this.read();
      if (previous) {
        if (previous.fingerprint !== fingerprint) return { status: 409, body: { error: "请求 ID 已被其他操作使用", code: "REQUEST_CONFLICT" } };
        const saved = JSON.parse(previous.result) as Result;
        return { status: saved.status, body: { ...saved.body, ...(state ? { snapshot: snapshot(state, now) } : {}) } };
      }
      if (!state && kind !== "open") return { status: 409, body: { error: "请先连接云端存档", code: "SESSION_REQUIRED" } };
      if (!state) {
        state = createGame(payload.uid, payload.name, now, payload.bootstrap);
        state.share = payload.share === true;
      }
      if (kind === "open") {
        if (state.session && state.session !== payload.session_id && state.leaseUntil > now && !payload.take_over) {
          return { status: 409, body: { error: "账号正在另一设备运行，可主动接管或等待原会话暂停", code: "SESSION_BUSY" } };
        }
      } else if (state.session !== payload.session_id) {
        return { status: 409, body: { error: "会话已结束或被其他设备接管，请重新连接", code: "SESSION_REPLACED" } };
      }
      advance(state, now);
      let status = 200, body: Record<string, unknown> = {};
      if (kind === "open" || kind === "heartbeat") {
        if (state.session !== payload.session_id) state.touchTotal = 0;
        state.session = payload.session_id;
        state.name = payload.name;
        updateBuffs(state, payload.buffs, payload.medal);
        state.leaseUntil = now + LEASE_MS;
        resumeGame(state, now);
      }
      const touches = number(payload.touch_total, 0, 999_999_999);
      if (touches > state.touchTotal) recordTouches(state, touches - state.touchTotal, now);
      state.touchTotal = Math.max(state.touchTotal, touches);
      if (kind === "close") {
        state.leaseUntil = now;
        state.session = "";
      } else if (kind === "command") {
        try {
          if (state.leaseUntil <= now) throw new GameError("当前已暂停，请等待重新连接", "SESSION_EXPIRED");
          command(state, payload.action!, now);
        } catch (error) {
          if (!(error instanceof GameError)) throw error;
          status = error.status;
          body = { error: error.message, code: error.code };
        }
      }
      this.write(state);
      this.ctx.storage.sql.exec("INSERT INTO requests(id, fingerprint, result, created_at) VALUES(?, ?, ?, ?)", payload.request_id, fingerprint, JSON.stringify({ status, body }), now);
      // Keep a bounded replay window, including rejected business operations.
      const expired = this.ctx.storage.sql.exec("DELETE FROM requests WHERE created_at < ? RETURNING id", now - 7 * 86400_000).toArray().length;
      const count = this.ctx.storage.sql.exec<{ count: number }>("SELECT count FROM request_history WHERE id=1").one().count + 1 - expired;
      const excess = Math.max(0, count - 4096);
      if (excess) this.ctx.storage.sql.exec("DELETE FROM requests WHERE id IN (SELECT id FROM requests ORDER BY created_at ASC, id ASC LIMIT ?)", excess);
      this.ctx.storage.sql.exec("UPDATE request_history SET count=? WHERE id=1", count - excess);
      return { status, body: { ...body, snapshot: snapshot(state, now) } };
    });
    await this.schedule();
    await this.publishRank();
    return result;
  }

  private async schedule(): Promise<void> {
    const state = this.read();
    if (!state) return;
    const now = Date.now();
    let due = state.session && state.leaseUntil > now ? state.leaseUntil : Infinity;
    if (due !== Infinity) due = Math.min(due, state.clockAt + 60_000 - state.expProgressMs);
    if (due !== Infinity && state.current) due = Math.min(due, state.clockAt + state.current.durationMs - state.current.elapsedMs);
    const dirty = this.ctx.storage.sql.exec<{ rank_dirty: number }>("SELECT rank_dirty FROM game WHERE id=1").one().rank_dirty;
    if (dirty) due = Math.min(due, now + 30_000);
    if (due === Infinity) await this.ctx.storage.deleteAlarm();
    else await this.ctx.storage.setAlarm(Math.max(now + 1, due));
  }

  private async publishRank(): Promise<void> {
    const row = this.ctx.storage.sql.exec<{ state: string; rank_dirty: number }>("SELECT state, rank_dirty FROM game WHERE id=1").toArray()[0];
    if (!row?.rank_dirty) return;
    const state = JSON.parse(row.state) as GameState;
    try {
      const a = state.attributes;
      await this.env.RANK.prepare(`INSERT INTO rankboard(uid,name,starcnt,exp,attr,revision,visible,updated_at)
        VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(uid) DO UPDATE SET name=excluded.name,starcnt=excluded.starcnt,
        exp=excluded.exp,attr=excluded.attr,revision=excluded.revision,visible=excluded.visible,updated_at=excluded.updated_at
        WHERE excluded.revision >= rankboard.revision`).bind(state.uid, state.name, state.stars, a.exp,
          a.speed + a.endurance + a.strength + a.will + a.intellect, state.revision, +state.share, Date.now()).run();
      this.ctx.storage.sql.exec("UPDATE game SET rank_dirty=0 WHERE id=1 AND json_extract(state, '$.revision') = ?", state.revision);
    } catch (error) {
      console.error(JSON.stringify({ message: "rank projection failed; retry scheduled", error: String(error) }));
    }
  }

  async alarm(): Promise<void> {
    this.ctx.storage.transactionSync(() => {
      const state = this.read();
      if (!state) return;
      advance(state, Date.now());
      this.write(state);
    });
    // Persist retry scheduling before doing external I/O.
    await this.schedule();
    this.broadcast();
    await this.publishRank();
    await this.schedule();
  }
}

function json(body: unknown, status = 200): Response {
  return Response.json(body, { status, headers: { "Cache-Control": "no-store" } });
}
async function body(request: Request): Promise<PlayerRequest> {
  const reader = request.body?.getReader();
  if (!reader) throw new GameError("缺少请求内容", "BAD_REQUEST", 400);
  const chunks: Uint8Array[] = []; let size = 0;
  while (true) {
    const part = await reader.read();
    if (part.done) break;
    size += part.value.length;
    if (size > MAX_REQUEST_BYTES) { await reader.cancel(); throw new GameError("请求内容过大", "BODY_TOO_LARGE", 413); }
    chunks.push(part.value);
  }
  const data = new Uint8Array(size); let offset = 0;
  for (const chunk of chunks) { data.set(chunk, offset); offset += chunk.length; }
  let value: PlayerRequest;
  try { value = JSON.parse(new TextDecoder().decode(data)); } catch { throw new GameError("无效 JSON", "BAD_REQUEST", 400); }
  return validate(value);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      const url = new URL(request.url);
      if (url.pathname.startsWith("/v1/ai/")) return routeAI(request, env);
      if (request.method === "GET" && url.pathname === "/health") return json({ ok: true, protocol: 1 });
      if (request.method === "GET" && url.pathname === "/v1/socket") {
        const uid = url.searchParams.get("uid");
        if (!validUid(uid)) throw new GameError("账号格式无效", "BAD_REQUEST", 400);
        if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return json({ error: "需要 WebSocket 升级" }, 426);
        return env.PLAYERS.getByName(uid).fetch(request);
      }
      if (request.method === "GET" && url.pathname === "/v1/rank") {
        const metric = url.searchParams.get("metric") || "starcnt";
        if (!["starcnt", "exp", "attr"].includes(metric)) throw new GameError("无效榜单", "BAD_REQUEST", 400);
        const limit = number(Number(url.searchParams.get("limit") || 100), 100, 100);
        const offset = number(Number(url.searchParams.get("offset") || 0), 0, 10000);
        const rows = await env.RANK.prepare(`SELECT uid,name,${metric} AS value,updated_at FROM rankboard WHERE visible=1 ORDER BY ${metric} DESC,uid ASC LIMIT ? OFFSET ?`).bind(Math.max(1, limit), offset).all();
        const uid = url.searchParams.get("uid");
        const me = uid ? await env.RANK.prepare(`SELECT uid,name,${metric} AS value,
          (SELECT COUNT(*)+1 FROM rankboard r WHERE r.visible=1 AND (r.${metric}>p.${metric} OR (r.${metric}=p.${metric} AND r.uid<p.uid))) AS rank
          FROM rankboard p WHERE p.uid=? AND p.visible=1`).bind(uid).first() : null;
        return json({ metric, offset, entries: rows.results, me });
      }
      const match = /^\/v1\/(open|heartbeat|command|close)$/.exec(url.pathname);
      if (request.method !== "POST" || !match) return json({ error: "接口不存在" }, 404);
      const payload = await body(request);
      if (match[1] === "command" && (!payload.action || typeof payload.action !== "object" || typeof payload.action.type !== "string")) throw new GameError("缺少操作指令", "BAD_REQUEST", 400);
      // Identity and external buffs are deliberately supplied by the trusted client.
      const result = JSON.parse(await env.PLAYERS.getByName(payload.uid).run(match[1] as Kind, payload)) as Result;
      return json(result.body, result.status);
    } catch (error) {
      if (error instanceof GameError) return json({ error: error.message, code: error.code }, error.status);
      console.error(JSON.stringify({ message: "request failed", error: String(error) }));
      return json({ error: "云端服务暂时不可用，请稍后重试", code: "SERVER_ERROR" }, 503);
    }
  },
} satisfies ExportedHandler<Env>;
