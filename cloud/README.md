# JPet Cloud

JPet 的游戏存档、任务结算和排行榜服务，部署在 `https://s.jpet.powerlive.io`（Cloudflare Workers）。
协议、存档规则和运维说明见[云端游戏说明](../doc/cloud-game.md)。

| 接口 | 用途 |
| --- | --- |
| GET `/health` | 健康检查 |
| GET `/v1/socket?uid=…` | 游戏会话 WebSocket，每个账号一个 `Player` Durable Object |
| GET `/v1/rank` | 星星、经验或属性排行榜（D1 `jpet-rank`） |

## 开发与部署

```sh
cd cloud
npm ci
npm run types
npm run typecheck
npm test
npm run deploy:check
npm run deploy
```

## 已移除的 JPet AI 服务

早期版本在这里提供 PowerLive 登录、按账号计算的 AI 额度，以及千问语音、视觉和搜索的转发（`/v1/ai/*`）。
桌面端改为本机语音识别，大模型和语音合成使用用户自己的配置后，这部分已经删除：

- `v3` 迁移删除 `AiAccount` Durable Object，部署后其中保存的额度记录随之删除；`Player` 存档和排行榜数据库不受影响。
- 部署后可以删除不再使用的 Worker secrets：`npx wrangler secret delete QWEN_WORKSPACE_ID`、`npx wrangler secret delete QWEN_API_KEY`。
