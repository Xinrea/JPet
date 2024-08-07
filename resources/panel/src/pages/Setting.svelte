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
  } from "flowbite-svelte";
  // audio
  let _volume = "20";
  let _mute = false;
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
  function init() {
    // get from server
    fetch("/api/config/audio")
      .then((res) => res.json())
      .then((data) => {
        _volume = data.volume;
        _mute = data.mute;
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
  }
  function updateAudio() {
    // post to server
    fetch("/api/config/audio", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ volume: parseInt(_volume), mute: _mute }),
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
  init();
</script>

<P class="mb-4">音频设置</P>
<Toggle class="mb-2" bind:checked={_mute} on:change={updateAudio}>静音</Toggle>
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
