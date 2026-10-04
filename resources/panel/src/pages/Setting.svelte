<script>
  import {
    Label,
    P,
    Toggle,
    Hr,
    Input,
    Badge,
    ButtonGroup,
    Button,
    Tooltip,
    Modal,
    Select,
    InputAddon,
  } from "flowbite-svelte";
  import QRCode from "qrcode";
  import fanAvatar from "../assets/fan.png";
  import { sse } from "../sse.js";
  import { onDestroy } from "svelte";
  let cloudUrl = "", cloudError = "", cloudMessage = "", cloudBusy = false, cloudOverride = false;
  async function saveCloud(takeOver = false) {
    cloudBusy = true; cloudError = ""; cloudMessage = "";
    try {
      const response = await fetch("/api/cloud", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ url: cloudUrl.trim(), take_over: takeOver }) });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "连接设置失败");
      cloudMessage = "已保存，正在连接云端";
    } catch (failure) { cloudError = failure.message; }
    finally { cloudBusy = false; }
  }
  // audio
  let _volume = "20";
  let _mute = false;
  let _touch_audio = false;
  let _idle_audio = false;
  // display
  let _green = false;
  let _limit = false;
  let _scale = "1.0";
  // notify
  let _uid = "";
  let _watch_list = [];
  let _dynamic = true;
  let _live = true;
  let _update = true;
  // other
  let _track = true;
  let _dropfile = true;
  let _shortcuts = [];
  const shortcut_types = [
    {value: 4, name: "禁用"},
    {
      value: 0,
      name: "程序",
    },
    { value: 1, name: "文件夹" },
    { value: 2, name: "网站" },
    { value: 3, name: "设置面板" },
  ];
  function init() {
    fetch("/api/cloud").then(res => res.json()).then(data => { cloudUrl = data.url || ""; cloudOverride = data.environment_override; }).catch(() => {});
    // get from server
    fetch("/api/config/audio")
      .then((res) => res.json())
      .then((data) => {
        _volume = data.volume;
        _mute = data.mute;
        _touch_audio = data.touch_audio;
        _idle_audio = data.idle_audio;
      });
    fetch("/api/config/display")
      .then((res) => res.json())
      .then((data) => {
        _green = data.green;
        _limit = data.limit;
        _scale = data.scale;
      });
    fetch("/api/config/notify")
      .then((res) => res.json())
      .then((data) => {
        console.log(data);
        _watch_list = data.watch_list ? data.watch_list : [];
        _dynamic = data.dynamic;
        _live = data.live;
        _update = data.update;
      });
    fetch("/api/config/shortcut")
      .then((res) => res.json())
      .then((data) => {
        _shortcuts = data;
      });
    fetch("/api/config/other")
      .then((res) => res.json())
      .then((data) => {
        _track = data.track;
        _dropfile = data.dropfile;
      });
    account_initial_timer = setTimeout(() => loadAccount(), 1000);
    account_refresh_timer = setInterval(() => loadAccount(), 10 * 1000);
    sse.subscribe((e) => {
      if (!e) {
        return;
      }
      console.log("SSE:", e);
      if (e.data == "NOTIFY_UPDATE") {
        fetch("/api/config/notify")
          .then((res) => res.json())
          .then((data) => {
            console.log(data);
            _watch_list = data.watch_list ? data.watch_list : [];
          });
      }
    });
  }
  function updateAudio() {
    // post to server
    fetch("/api/config/audio", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        volume: parseInt(_volume),
        mute: _mute,
        idle_audio: _idle_audio,
        touch_audio: _touch_audio,
      }),
    });
  }
  function updateDisplay() {
    // post to server
    fetch("/api/config/display", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        green: _green,
        limit: _limit,
        scale: parseFloat(_scale),
      }),
    });
  }
  function updateNotify() {
    // post to server
    fetch("/api/config/notify", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        dynamic: _dynamic,
        live: _live,
        update: _update,
      }),
    });
  }
  function updateShortcut(index, item) {
    fetch(`/api/config/shortcut/${index}`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(item),
    });
  }
  function updateOther() {
    // post to server
    fetch("/api/config/other", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        track: _track,
        dropfile: _dropfile,
      }),
    });
  }
  function removeWatch(id) {
    fetch("/api/config/notify", {
      method: "DELETE",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ uid: id }),
    })
      .then((res) => {
        console.log(res);
        return res.json();
      })
      .then((data) => {
        _watch_list = Array.isArray(data.watch_list) ? data.watch_list : [];
      });
  }
  function addWatch() {
    let body = JSON.stringify({ uid: _uid });
    fetch("/api/config/notify", {
      method: "PUT",
      headers: {
        "Content-Type": "application/json",
      },
      body: body,
    })
      .then((res) => res.json())
      .then((data) => {
        _watch_list = Array.isArray(data.watch_list) ? data.watch_list : [];
        _uid = "";
        console.log(_watch_list);
      });
  }
  let _reset = false;
  let resetModal = false;
  async function resetData() {
    try {
      const response = await fetch("/api/data/reset", { method: "POST" });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "重置失败");
      _reset = true;
    } catch (failure) { cloudError = failure.message; }
  }
  // acount
  export let account_info = null;
  let account_modal = false;
  let account_error = "";
  let qr_status = "等待扫码";
  let qr_error = "";
  let status_checker = null;
  let status_controller = null;
  let login_generation = 0;
  let account_refresh_timer = null;
  let account_initial_timer = null;
  let account_load_controller = null;
  let account_load_generation = 0;
  let qr_key = "";

  function invalidateAccountLoads() {
    account_load_generation += 1;
    if (account_load_controller) {
      account_load_controller.abort();
      account_load_controller = null;
    }
  }

  async function loadAccount() {
    const generation = account_load_generation;
    if (account_load_controller) account_load_controller.abort();
    const controller = new AbortController();
    account_load_controller = controller;
    try {
      const res = await fetch("/api/account", { signal: controller.signal });
      const data = await res.json();
      if (generation !== account_load_generation) return;
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      account_info = data;
      account_error = "";
    } catch (e) {
      if (e.name === "AbortError" || generation !== account_load_generation) {
        return;
      }
      console.error("Account refresh failed", e);
      account_error = `账号信息获取失败：${e.message || "网络错误"}`;
    } finally {
      if (account_load_controller === controller) {
        account_load_controller = null;
      }
    }
  }

  function stopQrPolling() {
    login_generation += 1;
    if (status_checker) {
      clearTimeout(status_checker);
      status_checker = null;
    }
    if (status_controller) {
      status_controller.abort();
      status_controller = null;
    }
  }

  async function pollQr(generation) {
    if (!account_modal || generation !== login_generation) return;
    const controller = new AbortController();
    status_controller = controller;
    try {
      const res = await fetch(
        `/api/account/qr-status?qrcode_key=${encodeURIComponent(qr_key)}`,
        { signal: controller.signal },
      );
      const data = await res.json();
      if (!account_modal || generation !== login_generation) return;
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      if (data.success) {
        qr_status = "登录成功，正在刷新账号信息…";
        invalidateAccountLoads();
        const accountRes = await fetch("/api/account");
        const accountData = await accountRes.json();
        if (!accountRes.ok) throw new Error(accountData.error || `HTTP ${accountRes.status}`);
        if (generation !== login_generation) return;
        account_info = accountData;
        account_error = "";
        account_modal = false;
        return;
      }
      if (data.code === 86038) {
        qr_status = "二维码已过期，请点击重试";
        login_generation += 1;
        return;
      }
      qr_status = data.code === 86090 ? "已扫码，请在手机上确认登录" : "等待扫码";
    } catch (e) {
      if (e.name === "AbortError" || generation !== login_generation) return;
      qr_error = `登录状态获取失败：${e.message || "网络错误"}`;
      qr_status = "";
      return;
    } finally {
      if (status_controller === controller) status_controller = null;
    }
    if (account_modal && generation === login_generation) {
      status_checker = setTimeout(() => pollQr(generation), 2000);
    }
  }

  async function doLogin() {
    stopQrPolling();
    const generation = login_generation;
    qr_status = "正在生成二维码…";
    qr_error = "";
    try {
      const res = await fetch("/api/account/qr");
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      qr_key = data.key || "";
      if (!qr_key) throw new Error("二维码缺少登录标识");
      if (!account_modal || generation !== login_generation) return;
      const canvas = document.getElementById("qrcode");
      await QRCode.toCanvas(canvas, data.url);
      if (!account_modal || generation !== login_generation) return;
      qr_status = "等待扫码";
      pollQr(generation);
    } catch (e) {
      if (generation !== login_generation) return;
      qr_status = "";
      qr_error = `二维码生成失败：${e.message || "网络错误"}`;
    }
  }
  async function logout() {
    invalidateAccountLoads();
    try {
      const res = await fetch("/api/account", { method: "DELETE" });
      const data = await res.json().catch(() => ({}));
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      account_info = { login: false, info: { confirm: false } };
      account_error = "";
    } catch (e) {
      account_error = `注销失败：${e.message || "网络错误"}`;
    }
  }
  init();
  onDestroy(() => {
    stopQrPolling();
    if (account_refresh_timer) clearInterval(account_refresh_timer);
    if (account_initial_timer) clearTimeout(account_initial_timer);
    invalidateAccountLoads();
  });
  $: if (!account_modal) stopQrPolling();
</script>

<div class="mb-5 rounded border border-gray-200 bg-white p-4">
  <Label class="mb-2" for="cloud-url">云端游戏服务</Label>
  <Input id="cloud-url" bind:value={cloudUrl} placeholder="https://你的服务.workers.dev" disabled={cloudOverride} />
  <p class="mt-2 text-xs text-gray-500">连接后同步云端存档。关闭或断网时，任务和经验暂停。首次连接会导入本机旧存档。</p>
  {#if cloudOverride}<p class="mt-2 text-xs text-gray-500">服务地址由启动配置指定。</p>{/if}
  <div class="mt-3 flex gap-2">
    <Button size="sm" disabled={cloudBusy} on:click={() => saveCloud()}>保存并连接</Button>
    <Button size="sm" color="alternative" disabled={cloudBusy} on:click={() => saveCloud(true)}>接管其他设备会话</Button>
  </div>
  {#if cloudError}<p class="mt-2 text-sm text-red-600" role="alert">{cloudError}</p>{/if}
  {#if cloudMessage}<p class="mt-2 text-sm text-green-600" role="status">{cloudMessage}</p>{/if}
</div>

<Modal bind:open={account_modal} on:open={doLogin}>
  <div class="flex justify-center">
    <canvas id="qrcode" />
  </div>
  {#if qr_status}<p class="mt-3 text-center">{qr_status}</p>{/if}
  {#if qr_error}
    <p class="mt-3 text-center text-red-600">{qr_error}</p>
    <div class="mt-3 flex justify-center">
      <Button on:click={doLogin}>重试</Button>
    </div>
  {:else if qr_status === "二维码已过期，请点击重试"}
    <div class="mt-3 flex justify-center">
      <Button on:click={doLogin}>重新生成二维码</Button>
    </div>
  {/if}
</Modal>
<P class="mb-4">账号设置</P>
{#if account_info && account_info.login}
  <div class="flex items-center space-x-4 rtl:space-x-reverse">
    {#key account_info.info.avatar}
      <img
        src={account_info.info.avatar || fanAvatar}
        alt={`${account_info.info.uname}的头像`}
        class="w-20 h-20 rounded object-cover bg-gray-100 dark:bg-gray-600"
        on:error={(event) => {
          const image = event.currentTarget;
          if (image.getAttribute("src") !== fanAvatar) image.src = fanAvatar;
        }}
      />
    {/key}
    <div class="space-y-1 font-medium dark:text-white">
      <div>{account_info.info.uname}</div>
      <div class="text-sm text-gray-500 dark:text-gray-400">
        轴芯等级：{account_info.info.level}
      </div>
      <Tooltip placement="right">等级能起到与智力类似的效果</Tooltip>
      <a
        href={"#"}
        on:click={logout}
        class="underline text-gray-500 decoration-green-500 decoration-2 text-sm"
        >注销登录</a
      >
    </div>
  </div>
{:else if account_info && !account_info.login}
  <Button
    on:click={() => {
      account_modal = true;
    }}>Bilibili 登录</Button
  >
{:else}
  <div class="text-sm text-gray-500 dark:text-gray-400">加载中</div>
  <a
    href={"#"}
    on:click={logout}
    class="underline text-gray-500 decoration-green-500 decoration-2 text-sm"
    >注销登录</a
  >
{/if}
{#if account_error}<p class="mt-2 text-sm text-red-600">{account_error}</p>{/if}
<Hr />
<P class="mb-4">音频设置</P>
<Toggle class="mb-2" bind:checked={_mute} on:change={updateAudio}>静音</Toggle>
{#if !_mute}
  <Toggle class="mb-2" bind:checked={_touch_audio} on:change={updateAudio}
    >互动语音</Toggle
  >
  <Toggle class="mb-2" bind:checked={_idle_audio} on:change={updateAudio}
    >闲置语音</Toggle
  >
{/if}
<Label for="volume">音量</Label>
<Input
  id="volume"
  bind:value={_volume}
  on:change={updateAudio}
  type="number"
  min={0}
  max={100}
  step={1}
/>
<Hr />
<P class="mb-4">显示设置</P>
<Toggle class="mb-2" bind:checked={_green} on:change={updateDisplay}
  >绿幕</Toggle
>
<Toggle class="mb-2" bind:checked={_limit} on:change={updateDisplay}
  >限制帧率</Toggle
>
<Label for="scale">缩放</Label>
<Input
  id="scale"
  type="number"
  min={0}
  max={3}
  step={0.1}
  bind:value={_scale}
  on:change={updateDisplay}
/>
<Hr />
<P class="mb-4">通知设置</P>
<Label class="mb-2">监控列表</Label>
<div class="mb-2">
  {#each _watch_list as item}
    <Badge dismissable class="mr-2" on:close={() => removeWatch(item.uid)}
      >{item.uname != "" ? item.uname : item.uid}</Badge
    >
  {/each}
</div>
<div class="mb-4">
  <ButtonGroup class="w-full">
    <Input placeholder="用户 UID" bind:value={_uid} />
    <Button color="primary" on:click={() => addWatch()}>添加</Button>
  </ButtonGroup>
</div>
<Toggle class="mb-2" bind:checked={_dynamic} on:change={updateNotify}
  >动态提醒</Toggle
>
<Toggle class="mb-2" bind:checked={_live} on:change={updateNotify}
  >直播提醒</Toggle
>
<Toggle class="mb-2" bind:checked={_update} on:change={updateNotify}
  >软件更新提醒</Toggle
>
<p class="text-xs">*由于 B 站风控政策，非登录状态不保证能够及时提醒。</p>
<Hr />
<P class="mb-4">轮盘菜单设置</P>
{#each _shortcuts as item, i}
  <div class="flex flex-row w-full mb-2">
    <Select
      class="w-1/3 mr-1"
      items={shortcut_types}
      bind:value={item.type}
      on:change={() => {
        item.param = "";
        updateShortcut(i, item);
      }}
    />
    {#if item.type == 2}
      <ButtonGroup class="w-full">
        <Input
          type="text"
          size="sm"
          bind:value={item.param}
          on:change={() => {
            updateShortcut(i, item);
          }}
        />
      </ButtonGroup>
    {:else if item.type == 3 || item.type == 4}
      <ButtonGroup class="w-full">
        <Input type="text" size="sm" disabled />
      </ButtonGroup>
    {:else}
      <ButtonGroup class="w-full">
        <Input
          type="text"
          size="sm"
          disabled
          bind:value={item.param}
          on:change={() => {
            updateShortcut(i, item);
          }}
        />
        <Button
          on:click={async () => {
            const t = item.type == 0 ? "file" : "folder";
            const resp = await (await fetch("/api/dialog/browse/" + t)).json();
            if (resp.success) {
              item.param = resp.path;
              updateShortcut(i, item);
            }
          }}
        >
          <svg
            xmlns="http://www.w3.org/2000/svg"
            fill="none"
            color="currentColor"
            class="shrink-0 h-6 w-6 text-slate-400"
            role="img"
            aria-label="folder open outline"
            viewBox="0 0 24 24"
            ><path
              stroke="currentColor"
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2"
              d="M3 19V6a1 1 0 0 1 1-1h4.032a1 1 0 0 1 .768.36l1.9 2.28a1 1 0 0 0 .768.36H16a1 1 0 0 1 1 1v1M3 19l3-8h15l-3 8H3Z"
            ></path></svg
          >
        </Button>
      </ButtonGroup>
    {/if}
  </div>
{/each}
<Hr />
<P class="mb-4">其它设置</P>
<Toggle class="mb-2" bind:checked={_track} on:change={updateOther}
  >鼠标追踪</Toggle
>
<Toggle class="mb-2" bind:checked={_dropfile} on:change={updateOther}
  >文件回收站</Toggle
>
<Hr />
<P class="mb-4">游戏数据设置</P>
<Button
  color="red"
  disabled={_reset}
  on:click={() => {
    resetModal = true;
  }}>{!_reset ? "重置云端游戏数据" : "已重置"}</Button
>
<Tooltip placement="right">重置当前账号的云端存档，立即生效</Tooltip>
<Modal title="确认重置" bind:open={resetModal} size="xs" autoclose>
  <h3 class="mb-5 text-lg font-normal text-gray-500">
    将清空当前账号的云端成长、任务和成就，确认重置吗？
  </h3>
  <Button
    color="red"
    on:click={() => {
      resetData();
    }}>确认</Button
  >
  <Button color="alternative">取消</Button>
</Modal>
