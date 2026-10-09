# JPet Cloud

JPet 的游戏存档服务和 AI 转发服务部署在 `https://s.jpet.powerlive.io`。

## PowerLive 登录与 AI

桌面端在系统浏览器中使用 PowerLive 登录，采用 OAuth Authorization Code + S256 PKCE，客户端 `jpet-desktop`，回调为本机 `http://127.0.0.1:<port>/oauth/callback`。需要 `openid profile email offline_access jpet:ai`，API 资源为 `jpet-api`。账号刷新令牌保存在 macOS 钥匙串 / Windows 凭据管理器，访问令牌仅保存在原生进程内存中。设置面板不接收令牌或 API Key。

AI 接口验证 PowerLive `https://api.powerlive.io` 的 RS256 签名、issuer、audience、有效期、账号 UUID、客户端和 scope。额度由独立的 SQLite Durable Object 按 PowerLive UUID 记账，同一账号的所有设备和 AI 模型共享额度；不依赖客户端上报。已签发的访问令牌最多有效 15 分钟，现有语音连接也会在令牌到期时关闭。

| 接口 | 功能 |
| --- | --- |
| GET `/v1/ai/me` | 服务配置状态及今日额度、已用量、预留量、剩余量、重置时间 |
| GET `/v1/ai/realtime` | WebSocket 语音转发，固定 `qwen3.8-omni-flash-realtime` |
| POST `/v1/ai/vision` | 桌面识别，固定 `qwen-vl-plus` |
| POST `/v1/ai/search` | 网页搜索，固定 `qwen-plus` |

每账号默认每日 **2,000,000 token**，按 **Asia/Shanghai 每日 00:00** 重置。管理员可以在 `wrangler.jsonc` 修改 `AI_DAILY_TOKEN_LIMIT` 后部署。自定义服务仍使用本机原有配置，不受该云端额度约束。

服务端先持久化预留额度再请求千问，以千问返回的 `usage` 结算。失败重试不会重复结算。未发送的预留会退款；已发送但中断或未返回有效用量的请求，保守地扣除预留额度。HTTP 工具请求预留 32,768 token；模型输出上限 1,500。语音按上下文动态预留、限制输出到 2,048 token，每账号只允许一条语音连接，最长 10 分钟，并在登录到期或北京时间零点前结束。语音上下文达到预算上限时需要重新开始。Durable Object 只持久化额度元数据，不保存提示词、截图、音频或凭据。

上游请求使用 `redirect: "manual"`，并拒绝重定向，避免把凭据发往其他主机。当前 Workers 运行时不接受 `redirect: "error"`，即使 TypeScript 类型允许该值。转发回归测试会用真实 Workers `Request` 校验请求选项，防止模拟 `fetch` 掩盖运行时错误。失败诊断日志仅记录阶段、状态和经过凭据脱敏的运行时错误，不记录请求头、音频、截图或对话。

语音握手使用可清除的 15 秒超时，收到升级响应后立即清除。不能直接使用 `AbortSignal.timeout(15000)`：Workers 会在取消信号触发时关闭已经升级的 WebSocket，导致第一轮正常、后续对话在连接后 15 秒断开。回归测试覆盖间隔超过 15 秒的两轮对话；已建立的连接仍由登录有效期、额度和最长 10 分钟会话规则控制。

千问启用 VAD 后会自动生成回复，代理在转发音频前预留当前响应和下一轮自动响应所需的额度，并在 `input_audio_buffer.committed` 时登记自动响应，不额外发送 `response.create`。只有工具结果需要显式续答；不依赖千问官方协议未定义的 `create_response` / `interrupt_response` 参数。用户打断工具时，已签发的调用 ID 可接收一次取消结果；未知 ID 和重复结果仍拒绝。参见[千问客户端事件](https://help.aliyun.com/zh/model-studio/client-events)。

被取消、不完整或失败的语音回复缺少有效 `usage` 时，只对该回复按发起时的预算结算一次，保留连接供下一轮使用；正常完成的回复缺少用量仍视为上游协议错误。待确认的工具续答也会随新语音标记为已打断，迟到的回复不再授权执行工具。跨过打断事件到达的旧续答请求会被忽略；重复的音频提交、回复建立及回复结束事件不会增加生成次数或重复记账。完整流程、边界与排查方式见[语音服务流程检查](../doc/ai-service-flow.md)。

麦克风采用按键开关，可持续多轮输入；静音等待不会触发原先的累计 62 秒限制，单段未结束的 VAD 语音仍有限制。音频费用按累计采样字节估算，避免对每个小音频包向上取整而放大预算。工具续答在缺少后续输入时，最多补发 1 秒纯静音 PCM；收到客户端音频、语音开始或回复建立事件即停止补帧，结束连接时清理定时器。补帧遵守现有预算，不打开本机麦克风。

## 配置与部署

千问业务空间和 API Key 仅通过 Worker secrets 配置，不能写入客户端或 Git：

```sh
cd cloud
npx wrangler secret put QWEN_WORKSPACE_ID
npx wrangler secret put QWEN_API_KEY
npm run types
npm run typecheck
npm test
npm run deploy:check
npm run deploy
```

使用北京业务空间；三个模型应具备访问权限。`v2` 迁移新增 `AiAccount` SQLite Durable Object，不修改既有 `Player` 游戏对象及排行榜数据库。PowerLive 已注册原生客户端，无需配置客户端密钥。
