# 云端游戏服务

JPet 使用 Cloudflare Workers 接收操作，每个 B 站 UID 对应一个 SQLite Durable Object。
该对象保存正式存档并计算经验、属性、任务、奖励、衣装和 50 个成就。D1 保存排行投影，
本地保留桌宠渲染、声音、窗口与装扮部件设置、B 站登录和加成检测。
客户端通过 WebSocket 长连接直接与玩家对象交换 JSON 消息，使用 Durable Objects 的
Hibernation WebSocket API，连接身份通过 attachment 保留，休眠后可继续接收消息。

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

开发客户端连接本地 Worker 时，在 CMake 配置阶段指定地址并重新构建，例如：

```sh
cmake -S . -B build/macos-arm64 -DJPET_CLOUD_URL=http://127.0.0.1:8787
cmake --build build/macos-arm64
```

`JPET_CLOUD_URL` 是构建参数。客户端不再读取同名运行时环境变量或 `jpet.toml` 的 `[cloud].url`，
设置页只提供重新连接与接管会话。地址必须使用 HTTPS，HTTP 仅允许 localhost 和 127.0.0.1。
该地址仍是 HTTPS 服务根地址；客户端自动转换为 WSS 并追加 `/v1/socket?uid=<UID>`，
本地 HTTP 地址对应 WS。macOS 使用 NSURLSessionWebSocketTask，Windows 使用 WinHTTP WebSocket。
恢复生产构建时，将 CMake 参数设为空：`-DJPET_CLOUD_URL=`。

未登录或连接失败时，面板展示缓存，成长与游戏操作暂停。声音、桌面交互等本地功能照常工作。

## 在线与暂停

- 客户端每 15 秒发送一次心跳，云端从接收时间授予 30 秒租约。
- 正常退出或注销时发送关闭会话请求，云端结算到接收时刻并暂停。
- 崩溃、突然断网时，仅推进至最后一次心跳的租约到期；最多有约 30 秒检测窗口。
- 任务和挂机经验累计有效在线时间，保留不足一分钟的经验进度和任务剩余时长。
- 重连后继续，离线时间不补算，排队任务在暂停期间也不启动。
- 同一 UID 同时只能有一个推进游戏的会话；另一设备需等待旧租约到期，或在设置中主动接管。
- 任务完成、经验分钟结算、租约到期和会话接管会主动推送，无需等待下一次心跳。
- 云端使用服务器时间和随机数；本地倒计时是显示插值，不生成奖励或修改属性。
- 自然日成就按 UTC+8 统计。

游戏时间在同步、操作和 Alarm 唤醒时按需结算。任务耗时以启动时的速度快照确定，
成功率按完成时的属性确定，与原规则一致。经验分钟结算、任务完成、加成更新按时间先后处理。
Alarm 按最早经验分钟结算、任务完成或租约到期时间安排；结算状态持久化后才回复和推送。

## 旧存档与缓存

首次启动新版本时，将本机旧成长、任务、队列、历史、成就保存在 `cloud.legacy_bootstrap` 备份。
首次成功连接的 UID 可导入这份数据；已有云端存档优先，后续连接不会再次导入。
旧版本的运行任务按迁移时可恢复的剩余时间导入，旧的待结算奖励在首次创建时结算一次。
本机旧存档不会自动复制给后续登录的其他 UID。

任务队列基础容量为 2，解锁第 3、4、5、6 个位置分别消耗 1、2、5、10 颗星星。
解锁次数独立保存在存档的 `queueUpgrades` 字段，扣费后容量不会减少，最多 6 个待执行位置；
运行中的任务不占容量。星星消耗后按剩余数量计算成长加成，超过新属性上限的部分按原规则转为经验，
已获得的成就永久保留。重置游戏数据会同时清空扩容记录。
存档 schema 1 会迁移至 schema 2，扩容记录从 0 开始；已有星星不会自动解锁或被扣除。
旧存档已排队的任务保留，即使超过新容量也会继续按顺序执行，只有空位出现后才能新增任务。
快照协议仍为 schema 1，`tasks.queue_upgrade` 返回下一档费用、当前星星数量及是否足够支付；
全部解锁后费用为 `null`。

云端快照按 UID 与服务地址保存为 `cloud.cache.<uid>.<url>`，不同地址的构建使用各自的缓存和版本。
客户端检查 UID 和递增版本，同版本时再比较 `server_time`，丢弃旧响应和重复推送；
重启后缓存保持暂停状态，直到云端连接成功。重置操作清空当前账号的云端游戏数据，立即生效，
保留账号、连接会话及排行榜参与设置。重新连接不会恢复重置前的本地数据。

## 请求与重试

客户端连接 `GET /v1/socket?uid=123456`，请求需带 WebSocket Upgrade。
随后在同一连接发送 `open`、`heartbeat`、`command`、`close` 消息，使用以下公共字段：

```json
{
  "type": "open",
  "uid": "123456",
  "name": "玩家昵称",
  "session_id": "客户端生成的随机会话ID",
  "request_id": "客户端生成的随机请求ID"
}
```

服务端确认消息示例：

```json
{
  "type": "response",
  "request_id": "对应的请求ID",
  "status": 200,
  "snapshot": {}
}
```

`status` 沿用 HTTP 状态含义；失败回复含 `error`、`code`。
主动推送使用 `{ "type": "snapshot", "snapshot": { ... } }`；被其他设备接管时收到
`{ "type": "session", "code": "SESSION_REPLACED", "error": "会话已被其他设备接管" }` 后关闭旧连接。
同一会话重连成功后会替换旧 socket；不同会话仍须遵守租约或主动接管。
每帧请求上限为 128 KiB，必须使用 JSON 文本帧。客户端确认超时为 8 秒，超时关闭连接并重试。

开始任务示例：`action: { "type": "task.start", "id": 3 }`。
其他操作包括 `task.queue`、`task.cancel`、`queue.move`、`queue.remove`、`queue.upgrade`、`attr.buy`、
`attr.refund`、`star`、`clothes`、`share` 和 `reset`。
心跳携带 `buffs`、`medal` 和本次会话累计 `touch_total`，操作和关闭请求也会结算未同步的抚摸次数。
排行榜参与选择保存在云端，通过 `share` 操作更新；心跳和其他设备的旧缓存不会覆盖这项选择。

操作请求发送前保存到本地 `cloud.pending`。连接失败、确认超时或服务端 5xx 时，客户端重试相同 ID；
云端在同一存储事务中记录状态和请求结果，成功和业务失败都可重放，防止重复扣费、领奖。
请求去重记录保留最近 7 天且最多 4096 条。客户端在上一条不确定操作确认前阻止提交新操作。
重连时先重放不确定操作，再打开会话；突然断开 socket 不会立即清除租约，避免已提交操作
丢失确认时无法恢复。正常退出仍发送 `close` 消息并等待确认，然后关闭 socket。

旧的 `POST /v1/open`、`/v1/heartbeat`、`/v1/command`、`/v1/close` 保留兼容，使用同一事务和去重记录。
排行榜继续使用 HTTPS GET。本机面板的 SSE 通知机制保持不变。

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
python3 tests/cloud_config_test.py
bash build-scripts/test_cloud_sync_macos.sh
bash build-scripts/test_achievements_macos.sh
```

云端测试使用 workerd、临时 Durable Objects 和本地 D1。原任务队列测试入口
`bash build-scripts/test_task_queue_macos.sh` 现运行云端任务与存储回归测试。
原 `preview` 参数已由上述本地开发流程替代。
WebSocket 回归覆盖休眠恢复、主动推送、账号隔离、会话接管、关闭与租约到期、消息大小限制、
重连去重；原生测试使用本机 WebSocket 故障注入服务模拟提交后回包丢失，不连接生产环境。

## 部署到 Cloudflare

### 生产环境

生产服务地址为 `https://s.jpet.powerlive.io`。CMake 在构建时读取 `cloud/wrangler.jsonc` 顶层
启用的 Custom Domain，生成 `JPetCloudConfig.hpp` 并将 HTTPS 地址编入客户端。
配置须包含唯一的启用域名；缺失或有歧义时构建报错。
修改生产域名后需要重新构建客户端，构建系统会重新生成地址。
生成头文件只包含服务地址，不包含账号 ID、数据库 ID 或凭据。

GitHub Actions 的 Windows x64 和 macOS ARM64 发布构建都使用这一流程，默认连接生产域名。
该工作流构建客户端；Worker 更新仍通过下文的部署命令执行。
发布 WebSocket 客户端前应先更新 Worker，使 `/v1/socket` 可用；旧 HTTP 客户端可继续连接。

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

确认 `wrangler.jsonc` 已设置目标 Custom Domain 后，重新构建客户端；
也可在 CMake 配置阶段用 `-DJPET_CLOUD_URL=https://目标服务域名` 指定构建地址。
`npm run deploy:check` 仅检查打包与配置，不创建云端资源，也不证明远程数据库已迁移。
游戏逻辑后续升级时需要保留状态字段和成就 ID，并按存档 schema 处理迁移。

已开启 Workers 日志和采样追踪。日志记录错误类型，不记录 Cookie、请求身份或完整游戏存档。
