import { createRemoteJWKSet, jwtVerify, type JWTVerifyGetKey } from "jose";

export const ISSUER = "https://api.powerlive.io";
const keys = createRemoteJWKSet(new URL(`${ISSUER}/oauth/jwks`));

export async function identity(request: Request, getKey: JWTVerifyGetKey = keys): Promise<{ sub: string; expires: number }> {
  const authorization = request.headers.get("Authorization") ?? "";
  if (!/^Bearer [^\s]{1,8192}$/.test(authorization)) throw new Error("Unauthorized");
  const { payload } = await jwtVerify(authorization.slice(7), getKey, {
    issuer: ISSUER, audience: "jpet-api", algorithms: ["RS256"], requiredClaims: ["sub", "exp", "iat"],
  });
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(payload.sub ?? "") ||
      payload.client_id !== "jpet-desktop" || typeof payload.scope !== "string" ||
      !payload.scope.split(" ").includes("jpet:ai")) throw new Error("Unauthorized");
  return { sub: payload.sub!, expires: payload.exp! * 1000 };
}

export function aiJson(body: unknown, status = 200): Response {
  return Response.json(body, { status, headers: { "Cache-Control": "no-store" } });
}

export async function routeAI(request: Request, env: Env): Promise<Response> {
  let account: Awaited<ReturnType<typeof identity>>;
  try { account = await identity(request); }
  catch { return aiJson({ error: "PowerLive 登录已失效，请重新登录", code: "jpet_login_required" }, 401); }
  const headers = new Headers(request.headers);
  // Overwrite any caller-supplied expiry. Tokens never reach the model provider.
  headers.delete("Authorization");
  headers.set("X-JPet-Expires", String(account.expires));
  try { return await env.AI_ACCOUNTS.getByName(account.sub).fetch(new Request(request, { headers })); }
  catch { return aiJson({ error: "AI 服务暂时不可用，请稍后重试", code: "jpet_unavailable" }, 503); }
}
