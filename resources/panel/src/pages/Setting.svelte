<script>
  import {
    Avatar,
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
    setTimeout(() => {
      fetch("/api/account")
        .then((res) => res.json())
        .then((data) => {
          account_info = data;
        });
    }, 1000);
    setInterval(async () => {
      const res = await fetch("/api/account");
      if (res.status == 200) {
        account_info = await res.json();
      }
    }, 10 * 1000);
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
        _watch_list = data.watch_list;
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
        _watch_list = data.watch_list;
        _uid = "";
        console.log(_watch_list);
      });
  }
  let _reset = false;
  let resetModal = false;
  function resetData() {
    _reset = true;
    fetch("/api/data/reset", { method: "POST" });
  }
  // acount
  let account_info = null;
  let account_modal = false;
  let status_checker = null;
  async function doLogin() {
    const qr_info = await (await fetch("/api/account/qr")).json();
    var canvas = document.getElementById("qrcode");
    QRCode.toCanvas(canvas, qr_info.url, (e) => {
      if (e) {
        console.error("Create QRCode failed", e);
      } else {
        console.log("QRCode updated");
      }
    });
    // release previous checker
    if (status_checker) {
      clearInterval(status_checker);
    }
    status_checker = setInterval(async () => {
      const res = await (await fetch("/api/account/qr-status")).json();
      if (res.success) {
        clearInterval(status_checker);
        account_modal = false;
        console.log("account confirmed");
        fetch("/api/account")
          .then((res) => res.json())
          .then((data) => {
            account_info = data;
          });
        return;
      }
      // if qrcode is closed, there is no need to check
      if (!account_modal) {
        clearInterval(status_checker);
      }
    }, 2000);
  }
  async function logout() {
    fetch("/api/account", { method: "DELETE" });
  }
  init();
</script>

<Modal bind:open={account_modal} on:open={doLogin}>
  <div class="flex justify-center">
    <canvas id="qrcode" />
  </div>
</Modal>
<P class="mb-4">账号设置</P>
{#if account_info && account_info.login}
  <div class="flex items-center space-x-4 rtl:space-x-reverse">
    <Avatar src={fanAvatar} rounded size="lg" />
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
  }}>{!_reset ? "重置数据" : "已标记重置"}</Button
>
<Tooltip placement="right">数据将会在重启后清除</Tooltip>
<Modal title="确认重置" bind:open={resetModal} size="xs" autoclose>
  <h3 class="mb-5 text-lg font-normal text-gray-500">
    此次操作无法取消，确认要进行吗？
  </h3>
  <Button
    color="red"
    on:click={() => {
      resetData();
    }}>确认</Button
  >
  <Button color="alternative">取消</Button>
</Modal>
