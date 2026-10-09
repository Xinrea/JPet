<script>
  import { onMount, onDestroy } from "svelte";
  import { Modal } from "flowbite-svelte";
  import VoiceSettings from "../components/VoiceSettings.svelte";
  import UiIcon from "../components/UiIcon.svelte";

  let voiceSettingsOpen = false;
  let records = [];
  let limit = 200;
  let loading = true;
  let refreshing = false;
  let error = "";
  let saveError = "";
  let search = "";
  let order = "recent";
  let timer;
  let controller;
  let destroyed = false;
  const states = { pending: "回复中", completed: "已完成", interrupted: "已打断", failed: "回复失败" };

  $: visible = records.filter((item) =>
    `${item.user}\n${item.assistant}`.toLocaleLowerCase().includes(search.trim().toLocaleLowerCase())
  ).sort((a, b) => order === "recent" ? b.id - a.id : a.id - b.id);

  function dateLabel(timestamp) {
    return new Date(timestamp * 1000).toLocaleString("zh-CN", {
      year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false,
    });
  }

  async function refresh() {
    if (destroyed || refreshing) return;
    refreshing = true;
    controller = new AbortController();
    try {
      const response = await fetch("/api/voice/history", { cache: "no-store", signal: controller.signal });
      if (!response.ok) throw new Error();
      const data = await response.json();
      if (!Array.isArray(data.list)) throw new Error();
      if (destroyed) return;
      records = data.list;
      limit = data.limit;
      saveError = data.error || "";
      error = "";
    } catch (failure) {
      if (!destroyed && failure.name !== "AbortError") error = "对话记录加载失败，请重试。";
    } finally {
      if (!destroyed) { loading = false; refreshing = false; }
    }
  }

  onMount(() => {
    refresh();
    timer = setInterval(() => { if (!document.hidden) refresh(); }, 1500);
  });
  onDestroy(() => {
    destroyed = true;
    clearInterval(timer);
    controller?.abort();
  });
</script>

<section class="conversation-page" aria-label="AI 对话">
  <div class="history-summary game-card">
    <span class="summary-icon"><UiIcon name="chat" size={26} /></span>
    <div class="summary-copy"><h2>和轴伊的聊天时光</h2><p>对话文字保存在本机，最多保留最近 {limit} 轮。</p></div>
    <div class="summary-actions">
      <span class="record-count"><strong>{records.length}</strong> 轮对话</span>
      <button class="voice-settings-button" aria-haspopup="dialog" on:click={() => voiceSettingsOpen = true}><UiIcon name="settings" size={17} />语音对话设置</button>
    </div>
  </div>

  <div class="history-tools">
    <label class="history-search"><span class="sr-only">搜索用户发言或轴伊回复</span><input type="search" bind:value={search} placeholder="搜索对话内容…" /></label>
    <label><span class="sr-only">对话排序</span><select bind:value={order}><option value="recent">最新在前</option><option value="oldest">最早在前</option></select></label>
    <button class="refresh-button" on:click={refresh} disabled={refreshing}>刷新</button>
  </div>

  {#if error || saveError}<div class="history-error" role="alert">{error || saveError}{#if error}<button on:click={refresh} disabled={refreshing}>重新加载</button>{/if}</div>{/if}
  {#if loading}
    <div class="history-empty game-card" role="status"><UiIcon name="chat" size={36} /><h2>正在读取对话记录…</h2></div>
  {:else if error && records.length === 0}
    <div class="history-empty game-card"><UiIcon name="chat" size={36} /><h2>暂时无法读取记录</h2><p>点击重新加载，再试一次。</p></div>
  {:else if records.length === 0}
    <div class="history-empty game-card"><UiIcon name="chat" size={36} /><h2>还没有对话记录</h2><p><button on:click={() => voiceSettingsOpen = true}>配置语音对话</button>后，按 Option（Mac）或 Ctrl（Windows）开启麦克风，和轴伊聊聊吧；再按一次关闭。</p><small>从本次更新后开始记录，语音转写仅供参考。</small></div>
  {:else if visible.length === 0}
    <div class="history-empty game-card"><UiIcon name="chat" size={36} /><h2>没有找到相关对话</h2><p>试试其他关键词，或<button on:click={() => search = ""}>清空搜索</button>。</p></div>
  {:else}
    <p class="result-count">显示 {visible.length} 轮对话 · 语音转写仅供参考</p>
    <div class="history-list">
      {#each visible as item (item.id)}
        <article class="conversation-card game-card">
          <header><time datetime={new Date(item.created_at * 1000).toISOString()}>{dateLabel(item.created_at)}</time><span class="turn-state" class:pending={item.state === "pending"} class:failed={item.state === "failed"}>{states[item.state] || "已完成"}</span></header>
          <div class="message user-message"><span class="message-avatar"><UiIcon name="user" size={18} /></span><div class="message-content"><h3>你</h3><p class:placeholder={!item.user}>{item.user || (item.state === "pending" ? "正在等待语音转写…" : "未获取到语音转写")}</p></div></div>
          <div class="message assistant-message"><span class="message-avatar"><UiIcon name="spark" size={18} /></span><div class="message-content"><h3>轴伊 <span>AI</span></h3><p class:placeholder={!item.assistant}>{item.assistant || (item.state === "pending" ? "正在思考或执行工具…" : item.state === "failed" ? "本次回复失败，请重新说话。" : item.state === "interrupted" ? "本次回复已打断。" : "本次没有文字回复。")}</p></div></div>
        </article>
      {/each}
    </div>
  {/if}
</section>

<Modal title="语音对话设置" bind:open={voiceSettingsOpen} size="md">
  {#if voiceSettingsOpen}<VoiceSettings />{/if}
</Modal>

<style>
  .history-summary { display: flex; align-items: center; gap: 14px; padding: 20px; margin-bottom: 20px; }
  .summary-icon { display: grid; place-items: center; flex-shrink: 0; width: 48px; height: 48px; color: var(--green-ink); background: var(--green-soft); border-radius: 14px; }
  .summary-copy { flex: 1; min-width: 0; }
  .summary-copy h2 { font-size: 16px; font-weight: 800; }
  .summary-copy p { margin-top: 6px; font-size: 12px; color: var(--muted); line-height: 1.8; }
  .summary-actions { display: flex; align-items: center; flex-wrap: wrap; gap: 16px; }
  .voice-settings-button { display: inline-flex; align-items: center; justify-content: center; gap: 7px; min-height: 40px; padding: 9px 14px; border: 1px solid #dce3d1; border-radius: 9px; background: var(--green-soft); color: var(--green-ink); font-size: 12px; font-weight: 800; }
  .voice-settings-button:hover { background: #e6f0d6; }
  .record-count { flex-shrink: 0; font-size: 11px; color: var(--muted); }
  .record-count strong { font-size: 26px; color: var(--green-ink); font-variant-numeric: tabular-nums; }
  .history-tools { display: flex; gap: 10px; margin-bottom: 16px; }
  .history-search { flex: 1; min-width: 0; }
  input, select, .refresh-button { min-height: 40px; border: 1px solid #dce3d1; border-radius: 9px; font-size: 12px; background: white; color: var(--ink); }
  input { width: 100%; padding: 9px 12px; }
  select { padding: 9px 30px 9px 12px; }
  .refresh-button { padding: 9px 16px; font-weight: 800; }
  .refresh-button:hover { background: var(--green-soft); }
  button:disabled { opacity: .55; cursor: wait; }
  .history-error { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 12px 16px; margin-bottom: 16px; border: 1px solid #efc7d1; border-radius: 10px; background: #fff3f5; color: #be5573; font-size: 12px; }
  .history-error button, .history-empty button { text-decoration: underline; font-weight: 700; }
  .history-empty { display: flex; align-items: center; flex-direction: column; gap: 12px; padding: 44px 24px; text-align: center; color: #96b377; }
  .history-empty h2 { font-size: 16px; color: var(--ink); font-weight: 800; }
  .history-empty p { max-width: 420px; font-size: 12px; line-height: 1.9; color: var(--muted); }
  .history-empty small { font-size: 11px; color: var(--muted); }
  .result-count { font-size: 11px; color: var(--muted); margin-bottom: 12px; }
  .history-list { display: flex; flex-direction: column; gap: 16px; }
  .conversation-card { overflow: hidden; }
  .conversation-card header { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 12px 18px; border-bottom: 1px solid #edf0e6; background: #fcfdf9; font-size: 11px; color: var(--muted); }
  .turn-state { padding: 3px 8px; border-radius: 6px; background: #edf3e5; color: #7e9569; }
  .turn-state.pending { background: #fff2d8; color: #aa8138; }
  .turn-state.failed { background: #fff0f4; color: #be5573; }
  .message { display: flex; align-items: flex-start; gap: 12px; margin: 18px; }
  .message-avatar { display: grid; place-items: center; width: 32px; height: 32px; flex-shrink: 0; color: #ae8974; background: #f7efe9; border-radius: 10px; }
  .assistant-message .message-avatar { color: var(--green-ink); background: #ecf6dd; }
  .message-content { flex: 1; min-width: 0; }
  .message-content h3 { font-size: 12px; font-weight: 800; margin: 3px 0 8px; }
  .message-content h3 span { margin-left: 4px; font-size: 9px; color: var(--green-ink); }
  .message-content p { font-size: 13px; line-height: 1.9; white-space: pre-wrap; overflow-wrap: anywhere; user-select: text; }
  .assistant-message .message-content p { padding: 12px 14px; border: 1px solid #e8eddc; border-radius: 10px; background: #f8fbf2; }
  .message-content p.placeholder { color: var(--muted); font-size: 12px; }
  @media (max-width: 560px) { .history-summary { flex-wrap: wrap; padding: 16px; gap: 10px; } .summary-icon { width: 40px; height: 40px; } .summary-actions { width: 100%; justify-content: space-between; gap: 10px; } .record-count strong { font-size: 20px; } .history-tools { flex-wrap: wrap; gap: 8px; } .history-search { flex-basis: 100%; } .history-tools > label:not(.history-search) { flex: 1; } select { width: 100%; } .message { margin: 16px 12px; gap: 9px; } .conversation-card header { padding: 12px; } }
</style>
