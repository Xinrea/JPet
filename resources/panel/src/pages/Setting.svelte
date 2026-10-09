<script>
  import { Input, Button, Modal, Select } from "flowbite-svelte";
  import SettingCard from "../components/SettingCard.svelte";
  import SettingToggle from "../components/SettingToggle.svelte";
  import UiIcon from "../components/UiIcon.svelte";
  export let online = false;
  let category = "general";
  const categories = [
    { id: "general", label: "通用", icon: "settings" },
    { id: "sound", label: "声音", icon: "sound" },
    { id: "notify", label: "通知", icon: "bell" },
    { id: "shortcut", label: "轮盘", icon: "wheel" },
  ];
  import QRCode from "qrcode";
  import fanAvatar from "../assets/fan.png";
  import { sse } from "../sse.js";
  import { onDestroy } from "svelte";
  let cloudError = "", cloudMessage = "", cloudBusy = false;
  async function reconnectCloud(takeOver = false) {
    cloudBusy = true; cloudError = ""; cloudMessage = "";
    try {
      const response = await fetch("/api/cloud/reconnect", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ take_over: takeOver }) });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "重新连接失败");
      cloudMessage = "正在连接云端";
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
  function loadLocalSettings() {
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
  }
  let settingsUnsubscribe;
  function init() {
    loadLocalSettings();
    account_initial_timer = setTimeout(() => loadAccount(), 1000);
    account_refresh_timer = setInterval(() => loadAccount(), 10 * 1000);
    settingsUnsubscribe = sse.subscribe((e) => {
      if (!e) {
        return;
      }
      console.log("SSE:", e);
      if (e.data === "SETTINGS_UPDATE") loadLocalSettings();
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
    settingsUnsubscribe?.();
    stopQrPolling();
    if (account_refresh_timer) clearInterval(account_refresh_timer);
    if (account_initial_timer) clearTimeout(account_initial_timer);
    invalidateAccountLoads();
  });
  $: if (!account_modal) stopQrPolling();
</script>

<div class="settings-page">
  <div class="settings-categories" aria-label="设置分类">
    {#each categories as item}
      <button class:selected={category === item.id} aria-pressed={category === item.id} on:click={() => category = item.id}>
        <UiIcon name={item.icon} size={17} />{item.label}
      </button>
    {/each}
  </div>

  <div class="settings-grid" class:section-hidden={category !== "general"}>
    <SettingCard title="账号设置" description="和轴伊一起开启每一天" icon="user">
      {#if account_info?.login}
        <div class="account-card">
          {#key account_info.info.avatar}
            <img src={account_info.info.avatar || fanAvatar} alt={`${account_info.info.uname}的头像`} class="account-avatar"
              on:error={(event) => { const image = event.currentTarget; if (image.getAttribute("src") !== fanAvatar) image.src = fanAvatar; }} />
          {/key}
          <div class="account-copy"><strong>{account_info.info.uname}</strong><span class="level-badge">轴芯等级 Lv.{account_info.info.level}</span><p>等级能起到与智力类似的效果</p></div>
        </div>
        <Button color="alternative" size="sm" on:click={logout}>注销登录</Button>
      {:else}
        <div class="account-card"><img src={fanAvatar} alt="" class="account-avatar" /><div class="account-copy"><strong>{account_info ? "还没有登录" : "正在读取账号…"}</strong><p>登录 B 站账号，获得轴芯等级加成。</p></div></div>
        <Button on:click={() => account_modal = true} size="sm"><UiIcon name="user" size={16} /><span class="ml-2">Bilibili 登录</span></Button>
      {/if}
      {#if account_error}<p class="feedback error" role="alert">{account_error}</p>{/if}
    </SettingCard>

    <SettingCard title="云端游戏服务" description="让成长与陪伴保持同步" icon="cloud">
      <div class="connection-label" class:connected={online}><span class="status-dot"></span>{online ? "云端已连接" : "等待云端连接"}</div>
      <p class="setting-help">连接后同步云端存档。关闭或断网时，任务和经验暂停。首次连接会导入本机旧存档。</p>
      <div class="setting-actions"><Button size="sm" disabled={cloudBusy} on:click={() => reconnectCloud()}>重新连接</Button><Button size="sm" color="alternative" disabled={cloudBusy} on:click={() => reconnectCloud(true)}>接管其他设备会话</Button></div>
      {#if cloudError}<p class="feedback error" role="alert">{cloudError}</p>{/if}
      {#if cloudMessage}<p class="feedback success" role="status">{cloudMessage}</p>{/if}
    </SettingCard>

    <SettingCard title="显示设置" description="调整适合你的桌面效果" icon="display">
      <SettingToggle label="绿幕" description="便于直播或录制时抠像" bind:checked={_green} on:change={updateDisplay} />
      <SettingToggle label="限制帧率" description="减少渲染时的资源占用" bind:checked={_limit} on:change={updateDisplay} />
      <div class="setting-field"><label for="scale">角色缩放<span>0 – 3 倍</span></label><Input id="scale" type="number" min={0} max={3} step={0.1} bind:value={_scale} on:change={updateDisplay} /></div>
    </SettingCard>

    <SettingCard title="桌面互动" description="让轴伊的陪伴更合心意" icon="cursor">
      <SettingToggle label="鼠标追踪" description="角色视线跟随鼠标移动" bind:checked={_track} on:change={updateOther} />
      <SettingToggle label="文件回收站" description="将文件拖给角色，移入回收站" bind:checked={_dropfile} on:change={updateOther} />
      <div class="setting-tip"><UiIcon name="spark" size={17} /><p>角色上的轮盘菜单，可以快速打开常用程序与网页。</p></div>
    </SettingCard>
  </div>

  <div class="settings-grid" class:section-hidden={category !== "sound"}>
    <SettingCard title="角色音频" description="熟悉的声音，随时陪在身边" icon="sound">
      <SettingToggle label="静音" description="关闭角色语音和对话回复声音" bind:checked={_mute} on:change={updateAudio} />
      {#if !_mute}
        <SettingToggle label="互动语音" description="触摸角色时播放语音" bind:checked={_touch_audio} on:change={updateAudio} />
        <SettingToggle label="闲置语音" description="闲置时偶尔播放角色语音" bind:checked={_idle_audio} on:change={updateAudio} />
      {/if}
      <div class="setting-field"><label for="volume">音量<span>{_volume}%</span></label><div class="volume-control"><input aria-label="音量滑块" type="range" min="0" max="100" step="1" bind:value={_volume} on:change={updateAudio} /><Input id="volume" bind:value={_volume} on:change={updateAudio} type="number" min={0} max={100} step={1} /></div></div>
    </SettingCard>


  </div>

  <div class="settings-grid" class:section-hidden={category !== "notify"}>
    <SettingCard title="关注列表" description="不错过关注用户的新消息" icon="user">
      <div class="watch-list">{#each _watch_list as item}<span class="watch-chip">{item.uname || item.uid}<button aria-label={`移除关注 ${item.uname || item.uid}`} on:click={() => removeWatch(item.uid)}>×</button></span>{:else}<p class="setting-help">还没有关注用户，添加一个 UID 开始接收提醒。</p>{/each}</div>
      <form class="watch-add" on:submit|preventDefault={addWatch}><Input aria-label="用户 UID" placeholder="输入 B 站用户 UID" bind:value={_uid} /><Button type="submit" size="sm" disabled={!_uid.trim()}>添加</Button></form>
    </SettingCard>
    <SettingCard title="通知偏好" description="选择你想收到的提醒" icon="bell">
      <SettingToggle label="动态提醒" description="关注用户发布新动态时通知" bind:checked={_dynamic} on:change={updateNotify} />
      <SettingToggle label="直播提醒" description="关注用户开播时通知" bind:checked={_live} on:change={updateNotify} />
      <SettingToggle label="软件更新提醒" description="发现 JPet 新版本时通知" bind:checked={_update} on:change={updateNotify} />
      <p class="setting-help">由于 B 站风控政策，非登录状态不保证能够及时提醒。</p>
    </SettingCard>
  </div>

  <div class:section-hidden={category !== "shortcut"}>
    <SettingCard title="轮盘菜单" description="四个方向，直达你的常用入口" icon="wheel">
      <p class="setting-help">为每个方向选择一种操作。程序和文件夹可通过右侧按钮选择，网站可直接填写地址。</p>
      {#each _shortcuts as item, i}
        <div class="shortcut-row"><span class="shortcut-direction">{["↑", "→", "↓", "←"][i]}<small>{["上", "右", "下", "左"][i]}</small></span>
          <Select aria-label={`${["上", "右", "下", "左"][i]}方操作类型`} items={shortcut_types} bind:value={item.type} on:change={() => { item.param = ""; updateShortcut(i, item); }} />
          <div class="shortcut-target">
            <Input aria-label={`${["上", "右", "下", "左"][i]}方操作目标`} type="text" bind:value={item.param} disabled={item.type != 2} placeholder={item.type == 3 ? "打开设置面板" : item.type == 4 ? "已禁用" : item.type == 2 ? "https://…" : "选择本机路径"} on:change={() => updateShortcut(i, item)} />
            {#if item.type == 0 || item.type == 1}<Button color="alternative" size="sm" aria-label={`选择${item.type == 0 ? "程序" : "文件夹"}`} on:click={async () => { const t = item.type == 0 ? "file" : "folder"; const resp = await (await fetch("/api/dialog/browse/" + t)).json(); if (resp.success) { item.param = resp.path; updateShortcut(i, item); } }}><UiIcon name="folder" size={20} /></Button>{/if}
          </div>
        </div>
      {/each}
    </SettingCard>
  </div>

</div>

<Modal title="Bilibili 扫码登录" bind:open={account_modal} on:open={doLogin} size="sm">
  <div class="qr-login"><p>使用哔哩哔哩手机客户端扫描二维码</p><div class="qr-frame"><canvas id="qrcode" /></div>
    {#if qr_status}<p role="status">{qr_status}</p>{/if}
    {#if qr_error}<p class="feedback error" role="alert">{qr_error}</p><Button on:click={doLogin}>重试</Button>{:else if qr_status === "二维码已过期，请点击重试"}<Button on:click={doLogin}>重新生成二维码</Button>{/if}
  </div>
</Modal>
