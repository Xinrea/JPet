// Beijing calendar days, shared by every AI model and device of one account.
export const DAILY_TOKENS = 2_000_000;
export const REQUEST_RESERVE = 32_768;
export const DAY_MS = 86_400_000;
export function quotaDay(now = Date.now()): number { return Math.floor((now + 8 * 3_600_000) / DAY_MS); }
export function quotaReset(now = Date.now()): number { return (quotaDay(now) + 1) * DAY_MS - 8 * 3_600_000; }

export interface QuotaView {
  limit: number; used: number; reserved: number; remaining: number;
  resets_at: number; timezone: string;
}

// Store only accounting metadata. Never store prompts, audio, images or credentials.
export class TokenQuota {
  constructor(private storage: DurableObjectStorage, private limit = DAILY_TOKENS) {
    storage.sql.exec(`CREATE TABLE IF NOT EXISTS ai_usage(day INTEGER PRIMARY KEY, tokens INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS ai_reservations(id TEXT PRIMARY KEY, day INTEGER NOT NULL,
        tokens INTEGER NOT NULL, expires_at INTEGER NOT NULL, exposed INTEGER NOT NULL DEFAULT 0,
        kind TEXT NOT NULL);
      CREATE UNIQUE INDEX IF NOT EXISTS ai_single_voice ON ai_reservations(kind) WHERE kind='realtime';`);
  }

  view(now = Date.now()): QuotaView {
    const day = quotaDay(now);
    const used = this.storage.sql.exec<{ tokens: number }>("SELECT tokens FROM ai_usage WHERE day=?", day).toArray()[0]?.tokens ?? 0;
    const reserved = this.storage.sql.exec<{ tokens: number }>("SELECT COALESCE(SUM(tokens),0) AS tokens FROM ai_reservations WHERE day=?", day).one().tokens;
    return { limit: this.limit, used, reserved, remaining: Math.max(0, this.limit - used - reserved),
      resets_at: quotaReset(now), timezone: "Asia/Shanghai" };
  }

  reserve(kind: "realtime" | "tool", now = Date.now(), amount = REQUEST_RESERVE): string | null {
    return this.storage.transactionSync(() => {
      this.expire(now);
      if (kind === "realtime" && this.storage.sql.exec("SELECT id FROM ai_reservations WHERE kind='realtime'").toArray().length) return null;
      const remaining = this.view(now).remaining;
      if (remaining < amount) return null;
      const id = crypto.randomUUID();
      this.storage.sql.exec("INSERT INTO ai_reservations(id,day,tokens,expires_at,kind) VALUES(?,?,?,?,?)",
        id, quotaDay(now), amount, Math.min(quotaReset(now), now + (kind === "realtime" ? 600_000 : 60_000)), kind);
      return id;
    });
  }

  ensure(id: string, amount: number): boolean {
    return this.storage.transactionSync(() => {
      this.expire();
      const row = this.storage.sql.exec<{ tokens: number }>("SELECT tokens FROM ai_reservations WHERE id=?", id).toArray()[0];
      if (!row || !Number.isSafeInteger(amount) || amount < 0) return false;
      const extra = Math.max(0, amount - row.tokens);
      if (this.view().remaining < extra) return false;
      this.storage.sql.exec("UPDATE ai_reservations SET tokens=tokens+? WHERE id=?", extra, id);
      return true;
    });
  }

  consume(id: string, tokens: number): boolean {
    return this.storage.transactionSync(() => {
      const row = this.storage.sql.exec<{ day: number; tokens: number }>("SELECT day,tokens FROM ai_reservations WHERE id=?", id).toArray()[0];
      if (!row || !Number.isSafeInteger(tokens) || tokens < 0) return false;
      this.storage.sql.exec("INSERT INTO ai_usage(day,tokens) VALUES(?,?) ON CONFLICT(day) DO UPDATE SET tokens=tokens+excluded.tokens", row.day, tokens);
      this.storage.sql.exec("UPDATE ai_reservations SET tokens=MAX(0,tokens-?) WHERE id=?", tokens, id);
      return tokens <= row.tokens;
    });
  }

  expose(id: string): void { this.storage.sql.exec("UPDATE ai_reservations SET exposed=1 WHERE id=?", id); }

  // The reservation is consumed exactly once, including retries and connection loss.
  settle(id: string, tokens: number | null): void {
    this.storage.transactionSync(() => {
      const row = this.storage.sql.exec<{ day: number; tokens: number; exposed: number }>(
        "DELETE FROM ai_reservations WHERE id=? RETURNING day,tokens,exposed", id).toArray()[0];
      if (!row) return;
      const charged = tokens === null ? (row.exposed ? row.tokens : 0) : tokens;
      if (!Number.isSafeInteger(charged) || charged < 0) throw new Error("Invalid token usage");
      this.storage.sql.exec("INSERT INTO ai_usage(day,tokens) VALUES(?,?) ON CONFLICT(day) DO UPDATE SET tokens=tokens+excluded.tokens", row.day, charged);
    });
  }

  expire(now = Date.now()): void {
    this.storage.transactionSync(() => {
      for (const row of this.storage.sql.exec<{ id: string }>("SELECT id FROM ai_reservations WHERE expires_at<=?", now).toArray()) this.settle(row.id, null);
      this.storage.sql.exec("DELETE FROM ai_usage WHERE day<?", quotaDay(now) - 7);
    });
  }

  nextExpiry(): number | null {
    return this.storage.sql.exec<{ due: number | null }>("SELECT MIN(expires_at) AS due FROM ai_reservations").one().due;
  }
}

// Support both compatible-mode and native DashScope usage; missing usage fails closed.
export function totalTokens(value: unknown): number | null {
  if (!value || typeof value !== "object") return null;
  const usage = value as Record<string, unknown>;
  if (Number.isSafeInteger(usage.total_tokens) && Number(usage.total_tokens) >= 0) return Number(usage.total_tokens);
  const input = usage.input_tokens ?? usage.prompt_tokens;
  const output = usage.output_tokens ?? usage.completion_tokens;
  if (Number.isSafeInteger(input) && Number(input) >= 0 && Number.isSafeInteger(output) && Number(output) >= 0) return Number.isSafeInteger(Number(input) + Number(output)) ? Number(input) + Number(output) : null;
  return null;
}
