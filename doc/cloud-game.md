# 云端游戏服务

JPet 使用 Cloudflare Workers 接收操作，每个 B 站 UID 对应一个 SQLite Durable Object。
该对象保存正式存档并计算经验、属性、任务、奖励、衣装和 50 个成就。D1 保存排行投影，
本地保留桌宠渲染、声音、窗口与装扮部件设置、B 站登录和加成检测。

身份（UID、昵称）和外部加成（直播、动态、舰长、周一、生日、轴芯等级）直接采信客户端。
B 站 Cookie 保留在本地，不会发送到此服务。任务失败加成依据云端失败次数计算。
服务没有额外的身份验证流程。

## 本地开发

需要 Node.js 22 或更新版本。依赖版本已在 `cloud/package-lock.json` 固定。

```sh
cd cloud
npm ci
npm run types
npm run db:local
npm run dev
```

在 JPet 设置页的「云端游戏服务」中填写 `http://127.0.0.1:8787`，保存并登录 B 站账号。
也可在启动时设置 `JPET_CLOUD_URL`；该变量优先于设置页和 `jpet.toml` 的 `[cloud].url`。
生产地址使用 HTTPS，HTTP 仅允许 localhost 和 127.0.0.1。

未配置服务、未登录或连接失败时，面板展示缓存，成长与游戏操作暂停。声音、桌面交互等本地功能照常工作。

## 在线与暂停

- 客户端每 15 秒发送一次心跳，云端从接收时间授予 30 秒租约。
- 正常退出或注销时发送关闭会话请求，云端结算到接收时刻并暂停。
- 崩溃、突然断网时，仅推进至最后一次心跳的租约到期；最多有约 30 秒检测窗口。
- 任务和挂机经验累计有效在线时间，保留不足一分钟的经验进度和任务剩余时长。
- 重连后继续，离线时间不补算，排队任务在暂停期间也不启动。
- 同一 UID 同时只能有一个推进游戏的会话；另一设备需等待旧租约到期，或在设置中主动接管。
- 云端使用服务器时间和随机数；本地倒计时是显示插值，不生成奖励或修改属性。
- 自然日成就按 UTC+8 统计。

游戏时间在同步、操作和 Alarm 唤醒时按需结算。任务耗时以启动时的速度快照确定，
成功率按完成时的属性确定，与原规则一致。经验分钟结算、任务完成、加成更新按时间先后处理。
Alarm 按最早任务完成或租约到期时间安排；结算状态持久化后才回复请求。

## 旧存档与缓存

首次启动新版本时，将本机旧成长、任务、队列、历史、成就保存在 `cloud.legacy_bootstrap` 备份。
首次成功连接的 UID 可导入这份数据；已有云端存档优先，后续连接不会再次导入。
旧版本的运行任务按迁移时可恢复的剩余时间导入，旧的待结算奖励在首次创建时结算一次。
本机旧存档不会自动复制给后续登录的其他 UID。

云端快照按 UID 与服务地址保存为 `cloud.cache.<uid>.<url>`，切换服务时单独保存缓存和版本。
客户端检查 UID 和递增版本，丢弃旧响应；
重启后缓存保持暂停状态，直到云端连接成功。重置操作清空当前账号的云端游戏数据，立即生效，
保留账号、连接会话及排行榜参与设置。重新连接不会恢复重置前的本地数据。

## 请求与重试

`POST /v1/open`、`/v1/heartbeat`、`/v1/command`、`/v1/close` 使用以下公共字段：

```json
{
  "uid": "123456",
  "name": "玩家昵称",
  "session_id": "客户端生成的随机会话ID",
  "request_id": "客户端生成的随机请求ID"
}
```

开始任务示例：`action: { "type": "task.start", "id": 3 }`。
其他操作包括 `task.queue`、`task.cancel`、`queue.move`、`queue.remove`、`attr.buy`、
`attr.refund`、`star`、`clothes`、`share` 和 `reset`。
心跳携带 `buffs`、`medal` 和本次会话累计 `touch_total`，操作和关闭请求也会结算未同步的抚摸次数。
排行榜参与选择保存在云端，通过 `share` 操作更新；心跳和其他设备的旧缓存不会覆盖这项选择。

操作请求发送前保存到本地 `cloud.pending`。连接失败或服务端 5xx 时，客户端重试相同 ID；
云端在同一存储事务中记录状态和请求结果，成功和业务失败都可重放，防止重复扣费、领奖。
请求去重记录保留最近 7 天且最多 4096 条。客户端在上一条不确定操作确认前阻止提交新操作。

`GET /v1/rank?metric=starcnt|exp|attr&offset=0&limit=100&uid=123456` 返回分页和个人排名。
同分按 UID 文本升序排列，用户主动参与后才公开。云端只在排行字段或参与设置变化时写 D1；
D1 故障不会阻止游戏存档更新，待同步标记持久化并由 Alarm 重试，旧版本不会覆盖新排行。

## 验证

```sh
cd cloud
npm run types
npm run typecheck
npm test
npm run deploy:check
cd ..
bash build-scripts/test_cloud_sync_macos.sh
bash build-scripts/test_achievements_macos.sh
```

云端测试使用 workerd、临时 Durable Objects 和本地 D1。原任务队列测试入口
`bash build-scripts/test_task_queue_macos.sh` 现运行云端任务与存储回归测试。
原 `preview` 参数已由上述本地开发流程替代。

## 部署到 Cloudflare

### 生产环境

生产服务地址为 `https://s.jpet.powerlive.io`。在 JPet 设置页的「云端游戏服务」中填写此地址，
或在启动时设置 `JPET_CLOUD_URL=https://s.jpet.powerlive.io`。

`cloud/wrangler.jsonc` 的顶层配置对应生产环境，固定以下资源：

- Worker：`jpet-cloud`，账号 ID：`507f35340f4c1062376b79dbbb011ce6`。
- 玩家存档：`PLAYERS` 绑定到 SQLite Durable Object 类 `Player`，首次部署通过 `v1` 迁移创建。
- 排行榜：`RANK` 绑定到 D1 数据库 `jpet-rank`，数据库 ID：`27fb48bb-efed-4428-8c22-9bc89ab5298c`，区域为 APAC。
- 域名：Workers Custom Domain `s.jpet.powerlive.io`，由 Cloudflare 管理 DNS 与 HTTPS 证书。
- 已开启日志和 1% 采样追踪，关闭 `workers.dev` 和版本预览地址。

更新生产服务时，复用配置中的资源，执行：

```sh
cd cloud
npm ci
npm run types
npm run typecheck
npm test
npm run deploy:check
npm run db:remote
npm run deploy
```

健康检查与排行榜检查：

```sh
curl --fail https://s.jpet.powerlive.io/health
curl --fail 'https://s.jpet.powerlive.io/v1/rank?metric=starcnt&limit=1'
```

健康检查返回 `{"ok":true,"protocol":1}`。`npm run dev`、本地数据库迁移和云端测试
仍使用本地模拟存储。

### 在新账号初始化

以下步骤仅用于在其他账号初始化服务；已有生产环境直接使用上述更新流程。
登录后先将 `wrangler.jsonc` 的 `account_id` 改为目标账号，将 `routes` 改为该账号下的自有域名，
并移除原有 `database_id`，再创建数据库：

```sh
cd cloud
npm ci
npx wrangler login
npx wrangler whoami
```

确认并修改目标账号和域名后执行：

```sh
npx wrangler d1 create jpet-rank
```

将创建返回的 `database_id` 填到 `wrangler.jsonc` 对应的 D1 绑定；多账号时明确设置目标 `account_id`。
然后执行：

```sh
npm run db:remote
npm run deploy
```

将部署返回的 Worker HTTPS 地址填到 JPet 设置页，或设置 `JPET_CLOUD_URL`。
`npm run deploy:check` 仅检查打包与配置，不创建云端资源，也不证明远程数据库已迁移。
游戏逻辑后续升级时需要保留状态字段和成就 ID，并按存档 schema 处理迁移。

已开启 Workers 日志和采样追踪。日志记录错误类型，不记录 Cookie、请求身份或完整游戏存档。
