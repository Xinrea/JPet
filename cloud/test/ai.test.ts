import { env } from "cloudflare:workers";
import { runInDurableObject, evictDurableObject } from "cloudflare:test";
import { describe, expect, it, beforeAll } from "vitest";
import { SignJWT, generateKeyPair, createLocalJWKSet, exportJWK } from "jose";
import { identity, ISSUER } from "../src/ai-auth";
import { DAILY_TOKENS, REQUEST_RESERVE, TokenQuota, quotaDay, quotaReset, totalTokens } from "../src/ai-quota";
import { validImage } from "../src/ai-service";
import worker from "../src/index";

describe("PowerLive AI authorization", () => {
  let privateKey: CryptoKey, keys: ReturnType<typeof createLocalJWKSet>;
  beforeAll(async () => {
    const pair = await generateKeyPair("RS256"); privateKey = pair.privateKey;
    keys = createLocalJWKSet({ keys: [{ ...await exportJWK(pair.publicKey), kid: "test", alg: "RS256" }] });
  });
  async function token(claims: Record<string, unknown> = {}, audience = "jpet-api", issuer = ISSUER) {
    return new SignJWT({ scope: "openid jpet:ai", client_id: "jpet-desktop", ...claims })
      .setProtectedHeader({ alg: "RS256", kid: "test" }).setSubject("8f1b9f5c-6865-4505-9d06-d540c597de94")
      .setIssuedAt().setExpirationTime("15m").setIssuer(issuer).setAudience(audience).sign(privateKey);
  }
  const request = (token: string) => new Request("https://jpet.test/v1/ai/me", { headers: { Authorization: `Bearer ${token}` } });
  it("requires a signed access token for the AI resource and registered native client", async () => {
    expect((await identity(request(await token()), keys)).sub).toBe("8f1b9f5c-6865-4505-9d06-d540c597de94");
    for (const invalid of [await token({}, "jpet-desktop"), await token({}, "jpet-api", "https://evil.test"), await token({ scope: "openid" }), await token({ client_id: "another-app" }), "invalid"]) {
      await expect(identity(request(invalid), keys)).rejects.toThrow();
    }
    const response = await worker.fetch(new Request("https://jpet.test/v1/ai/me?uid=10001"), env);
    expect(response.status).toBe(401);
    expect(await response.json()).toMatchObject({ code: "jpet_login_required" });
  });
});
describe("Account token ledger", () => {
  it("resets at Beijing midnight and rejects malformed upstream usage", () => {
    const before = Date.parse("2026-10-08T15:59:59.999Z"), after = before + 1;
    expect(quotaDay(after)).toBe(quotaDay(before) + 1); expect(quotaReset(before)).toBe(after);
    expect(totalTokens({ prompt_tokens: 123, completion_tokens: 45 })).toBe(168);
    expect(totalTokens({ total_tokens: -1 })).toBeNull(); expect(totalTokens({ total_tokens: 1.5 })).toBeNull();
    expect(totalTokens({ input_tokens: Number.MAX_SAFE_INTEGER, output_tokens: 10 })).toBeNull();
  });
  it("serializes reservations, bills exactly once and survives eviction", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, (instance) => {
      const ledger = instance.quota;
      const requests = Array.from({ length: Math.floor(DAILY_TOKENS / REQUEST_RESERVE) + 1 }, () => ledger.reserve("tool"));
      expect(requests.filter(Boolean)).toHaveLength(Math.floor(DAILY_TOKENS / REQUEST_RESERVE));
      ledger.expose(requests[0]!); ledger.settle(requests[0]!, 1234); ledger.settle(requests[0]!, 1234);
      for (const id of requests.slice(1).filter(Boolean)) ledger.settle(id!, 0);
      expect(ledger.view()).toMatchObject({ used: 1234, reserved: 0, remaining: DAILY_TOKENS - 1234 });
      const voice = ledger.reserve("realtime")!; expect(ledger.reserve("realtime")).toBeNull();
      expect(ledger.ensure(voice, 50000)).toBe(true); ledger.expose(voice);
      expect(ledger.consume(voice, 2500)).toBe(true);
      expect(ledger.view()).toMatchObject({ used: 3734, reserved: 47500 });
      ledger.settle(voice, 0);
    });
    await evictDurableObject(stub);
    await runInDurableObject(stub, instance => expect(instance.quota.view()).toMatchObject({ used: 3734, reserved: 0 }));
  });
  it("bills abandoned exposed requests and refunds untouched reservations on expiry", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, (instance, ctx) => {
      const ledger = new TokenQuota(ctx.storage, DAILY_TOKENS);
      const now = Date.parse("2026-10-08T15:59:00Z");
      const exposed = ledger.reserve("tool", now)!, untouched = ledger.reserve("tool", now)!;
      ledger.expose(exposed); expect(untouched).toBeTruthy(); ledger.expire(now + 60000);
      expect(ledger.view(now)).toMatchObject({ used: 32768, reserved: 0 });
      expect(ledger.view(now + 60000)).toMatchObject({ used: 0, reserved: 0, remaining: DAILY_TOKENS });
    });
  });
  it("raises an existing account's limit tenfold without resetting its usage or reservations", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, (_, ctx) => {
      const old = new TokenQuota(ctx.storage, 200_000);
      const paid = old.reserve("tool")!;
      old.expose(paid); old.settle(paid, 12500);
      const outstanding = old.reserve("tool")!;
      const upgraded = new TokenQuota(ctx.storage, DAILY_TOKENS);
      expect(upgraded.view()).toMatchObject({ limit: 2_000_000, used: 12500, reserved: REQUEST_RESERVE,
        remaining: 2_000_000 - 12500 - REQUEST_RESERVE, timezone: "Asia/Shanghai" });
      upgraded.settle(outstanding, 0);
    });
  });
  it("rejects invalid images and oversized JPEG dimensions", () => {
    expect(validImage("https://evil.test/image.jpg")).toBe(false);
    expect(validImage("data:image/jpeg;base64,eA==")).toBe(false);
    const jpeg = (w: number, h: number) => "data:image/jpeg;base64," + btoa(String.fromCharCode(255,216,255,192,0,11,8,h >> 8,h & 255,w >> 8,w & 255,1,1,0,0,255,217));
    expect(validImage(jpeg(1280,720))).toBe(true); expect(validImage(jpeg(65535,65535))).toBe(false);
  });
});

describe("AI forwarding and admission", () => {
  it("rejects upstream redirects without forwarding credentials and refunds the untouched request", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      let requests = 0;
      globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
        const request = new Request(input, init);
        expect(request.redirect).toBe("manual");
        expect(new URL(request.url).hostname).toBe("test-space.cn-beijing.maas.aliyuncs.com");
        requests++;
        return new Response(null, { status: 307, headers: { Location: "https://untrusted.example/collect" } });
      }) as typeof fetch;
      try {
        const request = new Request("https://jpet.test/v1/ai/search", { method: "POST", headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ input: { messages: [{ content: "unused" }, { content: "test question" }] } }) });
        expect((await instance.fetch(request)).status).toBe(502);
        expect(requests).toBe(1);
        expect(instance.quota.view()).toMatchObject({ used: 0, reserved: 0, remaining: DAILY_TOKENS });
      } finally { globalThis.fetch = original; }
    });
  });
  it("uses fixed upstream credentials/models and settles actual tool usage", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      let forwarded = 0;
      globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
        // Validate with the Workers runtime rather than allowing unsupported init options in the stub.
        new Request(input, init);
        forwarded++;
        expect(String(input)).toBe("https://test-space.cn-beijing.maas.aliyuncs.com/api/v1/services/aigc/text-generation/generation");
        expect(new Headers(init?.headers).get("Authorization")).toBe("Bearer test-key");
        const payload = JSON.parse(String(init?.body));
        expect(payload.model).toBe("qwen-plus"); expect(payload.parameters.max_tokens).toBe(1500);
        expect(payload.input.messages[1].content).toBe("test question");
        return Response.json({ output: { choices: [] }, usage: { input_tokens: 100, output_tokens: 23 } });
      }) as typeof fetch;
      try {
        const makeRequest = () => new Request("https://jpet.test/v1/ai/search", { method: "POST", headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ model: "attacker-model", input: { messages: [{ content: "override" }, { content: "test question" }] }, parameters: { max_tokens: 999999 } }) });
        expect((await instance.fetch(makeRequest())).status).toBe(200);
        expect(instance.quota.view()).toMatchObject({ used: 123, reserved: 0 });
        const reservation = instance.quota.reserve("tool", Date.now(), DAILY_TOKENS - 123)!;
        expect((await instance.fetch(makeRequest())).status).toBe(429);
        expect(forwarded).toBe(1); instance.quota.settle(reservation, 0);
        globalThis.fetch = (async () => Response.json({ output: { choices: [] } })) as typeof fetch;
        expect((await instance.fetch(makeRequest())).status).toBe(502);
        expect(instance.quota.view()).toMatchObject({ used: 123 + 32768, reserved: 0 });
      } finally { globalThis.fetch = original; }
    });
  });
  it("accepts automatic VAD generation, caps output, continues tools and bills duplicate events once", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      const [provider, upstream] = Object.values(new WebSocketPair()); provider.accept();
      let created = 0, audioChunks = 0, manualRequests = 0;
      const reply = () => {
        created++;
        const id = "response-" + created;
        provider.send(JSON.stringify({ type: "response.created", response: { id } }));
        const output = created === 1 ? [{ type: "function_call", call_id: "call-1", name: "get_game_state", arguments: "{}" }] : [];
        const done = JSON.stringify({ type: "response.done", response: { id, output, usage: { total_tokens: created === 1 ? 230 : 130, output_tokens: 80 } } });
        provider.send(done); provider.send(done);
      };
      provider.addEventListener("message", event => {
        const p = JSON.parse(event.data as string);
        if (p.type === "session.update") {
          expect(p.session.max_tokens).toBe(2048);
          provider.send(JSON.stringify({ type: "session.updated" }));
        } else if (p.type === "input_audio_buffer.append") {
          if (++audioChunks === 60) {
            provider.send(JSON.stringify({ type: "input_audio_buffer.committed", item_id: "audio-1" }));
            reply(); // Qwen VAD generates automatically after committing speech.
          }
        } else if (p.type === "response.create") {
          expect(p.response).toEqual({ modalities: ["text", "audio"] });
          manualRequests++; reply();
        }
      });
      globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
        new Request(input, init);
        return new Response(null, { status: 101, webSocket: upstream });
      }) as typeof fetch;
      let client: WebSocket | undefined;
      try {
        const response = await instance.fetch(new Request("https://jpet.test/v1/ai/realtime", { headers: { Upgrade: "websocket", "X-JPet-Expires": String(Date.now() + 300000) } }));
        expect(response.status).toBe(101); client = response.webSocket!; client.accept();
        const done = new Promise<void>((resolve, reject) => {
          const timer = setTimeout(() => reject(new Error("Realtime forwarding timeout")), 3000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "error") { clearTimeout(timer); reject(new Error(p.error.code)); }
            if (p.type === "session.updated") {
              for (let i = 0; i < 60; i++) client!.send(JSON.stringify({ type: "input_audio_buffer.append", audio: btoa("\0".repeat(3200)) }));
            }
            if (p.type === "response.done" && p.response.id === "response-1") {
              client!.send(JSON.stringify({ type: "conversation.item.create", item: { type: "function_call_output", call_id: "call-1", output: "{\"ok\":true}" } }));
              client!.send(JSON.stringify({ type: "response.create", response: { max_tokens: 999999 } }));
            }
            if (p.type === "response.done" && p.response.id === "response-2") { clearTimeout(timer); resolve(); }
          });
        });
        client.send(JSON.stringify({ type: "session.update", session: { instructions: "test", tools: [], audio: { input: { format: { type: "pcm", sample_rate: 16000 } } }, turn_detection: { create_response: true }, max_tokens: 999999 } }));
        await done;
        expect(audioChunks).toBe(60); expect(created).toBe(2); expect(manualRequests).toBe(1); expect(instance.quota.view().used).toBe(360);
        client.close(1000);
        // Wait for both sides' close events before inspecting the refunded reservation.
        await new Promise(resolve => setTimeout(resolve, 20));
        expect(instance.quota.view()).toMatchObject({ used: 360, reserved: 0 });
      } finally { client?.close(1000); provider.close(1000); globalThis.fetch = original; }
    });
  });
  it.each(["resume", "stall", "microphone"])("bounds continuation silence when the provider needs another audio frame: %s", async mode => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      const [provider, upstream] = Object.values(new WebSocketPair()); provider.accept();
      let continuation = false, audioAfterCreate = 0, manualRequests = 0, toolResults = 0;
      const emit = (p: unknown) => provider.send(JSON.stringify(p));
      const done = (id: string, output: unknown[] = []) => {
        emit({ type: "response.created", response: { id } });
        emit({ type: "response.done", response: { id, status: "completed", output, usage: { total_tokens: 50, output_tokens: 10 } } });
      };
      provider.addEventListener("message", event => {
        const p = JSON.parse(event.data as string);
        if (p.type === "session.update") emit({ type: "session.updated" });
        if (p.type === "input_audio_buffer.append") {
          if (!continuation) {
            emit({ type: "input_audio_buffer.committed", item_id: "input-1" });
            done("tool-request", [{ type: "function_call", call_id: "tool-1", name: "get_game_state", arguments: "{}" }]);
          } else {
            audioAfterCreate++;
            expect(atob(p.audio)).toBe((mode === "microphone" ? "\x01" : "\0").repeat(mode === "microphone" ? 3200 : 6400));
            if (mode === "resume" || mode === "microphone") done("tool-answer");
          }
        }
        if (p.type === "conversation.item.create") toolResults++;
        if (p.type === "response.create") { manualRequests++; continuation = true; }
      });
      globalThis.fetch = (async () => new Response(null, { status: 101, webSocket: upstream })) as typeof fetch;
      let client: WebSocket | undefined, deadline: ReturnType<typeof setTimeout> | undefined;
      let failureCode = "";
      try {
        const response = await instance.fetch(new Request("https://jpet.test/v1/ai/realtime", { headers: { Upgrade: "websocket", "X-JPet-Expires": String(Date.now() + 300000) } }));
        client = response.webSocket!; client.accept();
        const send = (p: unknown) => client!.send(JSON.stringify(p));
        await new Promise<void>((resolve, reject) => {
          deadline = setTimeout(() => reject(new Error("Continuation did not start")), 3000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "error") { failureCode = p.error.code; reject(new Error(failureCode)); }
            if (p.type === "session.updated") send({ type: "input_audio_buffer.append", audio: btoa("\x01".repeat(3200)) });
            if (p.type === "response.done" && p.response.id === "tool-request") {
              send({ type: "conversation.item.create", item: { type: "function_call_output", call_id: "tool-1", output: '{"ok":true}' } });
              send({ type: "response.create" });
              if (mode === "microphone") send({ type: "input_audio_buffer.append", audio: btoa("\x01".repeat(3200)) });
              if (mode === "stall") resolve();
            }
            if (p.type === "response.done" && p.response.id === "tool-answer") resolve();
          });
          send({ type: "session.update", session: { instructions: "test", tools: [], audio: { input: { format: { type: "pcm", sample_rate: 16000 } } } } });
        });
        if (deadline !== undefined) clearTimeout(deadline);
        // A responsive provider stops padding immediately; a stalled one gets
        // at most one second. Actual microphone input cancels the fallback.
        await new Promise(resolve => setTimeout(resolve, 1400));
        expect(failureCode).toBe(""); expect(manualRequests).toBe(1); expect(toolResults).toBe(1);
        expect(audioAfterCreate).toBe(mode === "stall" ? 5 : 1);
        if (mode !== "stall") expect(instance.quota.view().used).toBe(100);
      } finally { if (deadline !== undefined) clearTimeout(deadline); client?.close(1000); provider.close(1000); globalThis.fetch = original; }
    });
  });
  it("accepts a new spoken turn after more than a minute of microphone-on silence", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      const [provider, upstream] = Object.values(new WebSocketPair()); provider.accept();
      let silentBytes = 0;
      provider.addEventListener("message", event => {
        const p = JSON.parse(event.data as string);
        if (p.type === "session.update") provider.send(JSON.stringify({ type: "session.updated" }));
        if (p.type === "input_audio_buffer.append") {
          const pcm = atob(p.audio);
          if (pcm.charCodeAt(0) === 0) silentBytes += pcm.length;
          else {
            provider.send(JSON.stringify({ type: "input_audio_buffer.committed", item_id: "after-silence" }));
            provider.send(JSON.stringify({ type: "response.created", response: { id: "answer" } }));
            provider.send(JSON.stringify({ type: "response.done", response: { id: "answer", status: "completed", output: [], usage: { total_tokens: 50, output_tokens: 10 } } }));
          }
        }
      });
      globalThis.fetch = (async () => new Response(null, { status: 101, webSocket: upstream })) as typeof fetch;
      let client: WebSocket | undefined, deadline: ReturnType<typeof setTimeout> | undefined;
      try {
        const response = await instance.fetch(new Request("https://jpet.test/v1/ai/realtime", { headers: { Upgrade: "websocket", "X-JPet-Expires": String(Date.now() + 300000) } }));
        client = response.webSocket!; client.accept();
        await new Promise<void>((resolve, reject) => {
          deadline = setTimeout(() => reject(new Error("Silent microphone disconnected")), 3000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "error") reject(new Error(p.error.code));
            if (p.type === "session.updated") {
              for (let i = 0; i < 65; ++i) client!.send(JSON.stringify({ type: "input_audio_buffer.append", audio: btoa("\0".repeat(32000)) }));
              client!.send(JSON.stringify({ type: "input_audio_buffer.append", audio: btoa("\x01".repeat(3200)) }));
            }
            if (p.type === "response.done") resolve();
          });
          client!.send(JSON.stringify({ type: "session.update", session: { instructions: "test", tools: [], audio: { input: { format: { type: "pcm", sample_rate: 16000 } } } } }));
        });
        expect(silentBytes).toBe(65 * 32000); expect(instance.quota.view().used).toBe(50);
      } finally { if (deadline !== undefined) clearTimeout(deadline); client?.close(1000); provider.close(1000); globalThis.fetch = original; }
    });
  });
  it("accepts cancellation output for an issued tool after VAD interruption", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      const [provider, upstream] = Object.values(new WebSocketPair()); provider.accept();
      let turns = 0, cancelledTools = 0;
      provider.addEventListener("message", event => {
        const p = JSON.parse(event.data as string);
        if (p.type === "session.update") provider.send(JSON.stringify({ type: "session.updated" }));
        if (p.type === "input_audio_buffer.append") {
          const id = "interrupt-" + ++turns;
          provider.send(JSON.stringify({ type: "input_audio_buffer.committed", item_id: id }));
          provider.send(JSON.stringify({ type: "response.created", response: { id } }));
          const output = turns === 1 ? [{ type: "function_call", call_id: "pending-tool", name: "bilibili_search", arguments: "{}" }] : [];
          provider.send(JSON.stringify({ type: "response.done", response: { id, status: "completed", output, usage: { total_tokens: 50, output_tokens: 10 } } }));
        }
        if (p.type === "conversation.item.create") {
          cancelledTools++;
          expect(p.item.call_id).toBe("pending-tool");
          provider.send(JSON.stringify({ type: "conversation.item.created" }));
        }
      });
      globalThis.fetch = (async () => new Response(null, { status: 101, webSocket: upstream })) as typeof fetch;
      let client: WebSocket | undefined;
      try {
        const response = await instance.fetch(new Request("https://jpet.test/v1/ai/realtime", { headers: { Upgrade: "websocket", "X-JPet-Expires": String(Date.now() + 300000) } }));
        expect(response.status).toBe(101); client = response.webSocket!; client.accept();
        const append = () => client!.send(JSON.stringify({ type: "input_audio_buffer.append", audio: btoa("\0".repeat(3200)) }));
        await new Promise<void>((resolve, reject) => {
          const deadline = setTimeout(() => reject(new Error("Interrupted tool did not close")), 3000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "session.updated" || p.type === "conversation.item.created") append();
            if (p.type === "response.done" && p.response.id === "interrupt-1") provider.send(JSON.stringify({ type: "input_audio_buffer.speech_started" }));
            if (p.type === "input_audio_buffer.speech_started") client!.send(JSON.stringify({ type: "conversation.item.create", item: { type: "function_call_output", call_id: "pending-tool", output: '{"ok":false,"error":"用户已打断"}' } }));
            if (p.type === "error") { if (deadline !== undefined) clearTimeout(deadline); reject(new Error(p.error.code)); }
            if (p.type === "response.done" && p.response.id === "interrupt-2") { if (deadline !== undefined) clearTimeout(deadline); resolve(); }
          });
          client!.send(JSON.stringify({ type: "session.update", session: { instructions: "test", tools: [], audio: { input: { format: { type: "pcm", sample_rate: 16000 } } } } }));
        });
        expect(cancelledTools).toBe(1); expect(turns).toBe(2); expect(instance.quota.view().used).toBe(100);
        // Retired IDs remain one-use; a replay cannot inject another tool result.
        await new Promise<void>((resolve, reject) => {
          const deadline = setTimeout(() => reject(new Error("Replayed tool output was accepted")), 3000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "error") { if (deadline !== undefined) clearTimeout(deadline); expect(p.error.code).toBe("jpet_upstream_error"); resolve(); }
          });
          client!.send(JSON.stringify({ type: "conversation.item.create", item: { type: "function_call_output", call_id: "pending-tool", output: "{}" } }));
        });
        expect(cancelledTools).toBe(1);
      } finally { client?.close(1000); provider.close(1000); globalThis.fetch = original; }
    });
  });
  it("keeps two voice turns connected beyond the 15-second handshake deadline", async () => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      const [provider, upstream] = Object.values(new WebSocketPair()); provider.accept();
      let signal: AbortSignal | undefined, created = 0;
      provider.addEventListener("message", event => {
        const p = JSON.parse(event.data as string);
        if (p.type === "session.update") provider.send(JSON.stringify({ type: "session.updated" }));
        if (p.type === "input_audio_buffer.append") {
          provider.send(JSON.stringify({ type: "input_audio_buffer.committed" }));
          const id = "long-turn-" + ++created;
          provider.send(JSON.stringify({ type: "response.created", response: { id } }));
          provider.send(JSON.stringify({ type: "response.done", response: { id, status: "completed", output: [], usage: { total_tokens: 42, output_tokens: 10 } } }));
        }
      });
      globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
        signal = new Request(input, init).signal;
        // Real Workers fetch closes an upgraded connection when its signal
        // aborts. Preserve that behavior instead of hiding it in the stub.
        signal.addEventListener("abort", () => provider.close(1000));
        return new Response(null, { status: 101, webSocket: upstream });
      }) as typeof fetch;
      let client: WebSocket | undefined, later: ReturnType<typeof setTimeout> | undefined;
      try {
        const response = await instance.fetch(new Request("https://jpet.test/v1/ai/realtime", { headers: { Upgrade: "websocket", "X-JPet-Expires": String(Date.now() + 300000) } }));
        expect(response.status).toBe(101); client = response.webSocket!; client.accept();
        const append = () => client!.send(JSON.stringify({ type: "input_audio_buffer.append", audio: btoa("\0".repeat(3200)) }));
        await new Promise<void>((resolve, reject) => {
          const deadline = setTimeout(() => reject(new Error("Second voice turn timed out")), 19000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "session.updated") append();
            if (p.type === "response.done" && p.response.id === "long-turn-1") later = setTimeout(append, 16000);
            if (p.type === "error") { if (deadline !== undefined) clearTimeout(deadline); reject(new Error(p.error.code)); }
            if (p.type === "response.done" && p.response.id === "long-turn-2") { if (deadline !== undefined) clearTimeout(deadline); resolve(); }
          });
          client!.send(JSON.stringify({ type: "session.update", session: { instructions: "test", tools: [], audio: { input: { format: { type: "pcm", sample_rate: 16000 } } } } }));
        });
        expect(signal?.aborted).toBe(false); expect(created).toBe(2); expect(instance.quota.view().used).toBe(84);
        client.close(1000);
        await new Promise(resolve => setTimeout(resolve, 20));
        expect(instance.quota.view()).toMatchObject({ used: 84, reserved: 0 });
      } finally { if (later !== undefined) clearTimeout(later); client?.close(1000); provider.close(1000); globalThis.fetch = original; }
    });
  }, 22000);
  it.each(["cancelled", "incomplete", "failed"])("continues after a %s reply without usage, with delayed tool continuation and duplicate events", async status => {
    const stub = env.AI_ACCOUNTS.getByName(crypto.randomUUID());
    await runInDurableObject(stub, async instance => {
      const original = globalThis.fetch;
      const [provider, upstream] = Object.values(new WebSocketPair()); provider.accept();
      let turns = 0, manual = 0, firstBill = 0;
      const emit = (p: unknown) => provider.send(JSON.stringify(p));
      provider.addEventListener("message", event => {
        const p = JSON.parse(event.data as string);
        if (p.type === "session.update") emit({ type: "session.updated" });
        if (p.type === "input_audio_buffer.append") {
          const id = "race-" + ++turns;
          const commit = { type: "input_audio_buffer.committed", item_id: "input-" + turns };
          emit(commit); emit(commit);
          const created = { type: "response.created", response: { id } };
          emit(created); emit(created);
          if (turns === 1) emit({ type: "response.done", response: { id, status: "completed", output: [
            { type: "function_call", call_id: "search-tool", name: "bilibili_search", arguments: "{}" },
          ], usage: { total_tokens: 50, output_tokens: 10 } } });
          else emit({ type: "response.done", response: { id, status: "completed", output: [], usage: { total_tokens: 42, output_tokens: 10 } } });
        }
        if (p.type === "response.create") {
          manual++;
          // The user speaks while the provider has not yet acknowledged the
          // tool continuation. Cancellation and duplicate done arrive later.
          emit({ type: "input_audio_buffer.speech_started" });
          emit({ type: "response.created", response: { id: "late-tool-reply" } });
          const done = { type: "response.done", response: { id: "late-tool-reply", status, output: [] } };
          emit(done); emit(done);
        }
      });
      globalThis.fetch = (async () => new Response(null, { status: 101, webSocket: upstream })) as typeof fetch;
      let client: WebSocket | undefined;
      try {
        const response = await instance.fetch(new Request("https://jpet.test/v1/ai/realtime", { headers: { Upgrade: "websocket", "X-JPet-Expires": String(Date.now() + 300000) } }));
        client = response.webSocket!; client.accept();
        const send = (p: unknown) => client!.send(JSON.stringify(p));
        const append = () => send({ type: "input_audio_buffer.append", audio: btoa("\0".repeat(3200)) });
        await new Promise<void>((resolve, reject) => {
          const deadline = setTimeout(() => reject(new Error("Interrupted continuation could not recover")), 3000);
          client!.addEventListener("message", event => {
            const p = JSON.parse(event.data as string);
            if (p.type === "session.updated") append();
            if (p.type === "response.done" && p.response.id === "race-1") {
              send({ type: "conversation.item.create", item: { type: "function_call_output", call_id: "search-tool", output: "{}" } });
              send({ type: "response.create" });
            }
            if (p.type === "response.done" && p.response.id === "late-tool-reply") {
              firstBill = instance.quota.view().used;
              // An old continuation racing the new speech must not request a
              // second generation or terminate the socket.
              send({ type: "response.create" }); append();
            }
            if (p.type === "error") { if (deadline !== undefined) clearTimeout(deadline); reject(new Error(p.error.code)); }
            if (p.type === "response.done" && p.response.id === "race-2") { if (deadline !== undefined) clearTimeout(deadline); resolve(); }
          });
          send({ type: "session.update", session: { instructions: "test", tools: [], audio: { input: { format: { type: "pcm", sample_rate: 16000 } } } } });
        });
        expect(turns).toBe(2); expect(manual).toBe(1);
        expect(firstBill).toBeGreaterThan(50);
        expect(instance.quota.view().used).toBe(firstBill + 42);
        client.close(1000); await new Promise(resolve => setTimeout(resolve, 20));
        expect(instance.quota.view()).toMatchObject({ used: firstBill + 42, reserved: 0 });
      } finally { client?.close(1000); provider.close(1000); globalThis.fetch = original; }
    });
  });
});
