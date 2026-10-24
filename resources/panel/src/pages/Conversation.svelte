<script>
  import { onMount, onDestroy, tick } from "svelte";
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
  let sendError = "";
  let search = "";
  let searchOpen = false;
  let draft = "";
  let sending = false;
  let status = { state: "idle", message: "", replying: false, shortcut: "Option" };
  let dismissedError = "";
  let page, input;
  let timer;
  let controller;
  let destroyed = false;
  let stickToBottom = true;
  const states = { pending: "进行中", completed: "已完成", interrupted: "已打断", failed: "失败" };
  const tools = { view_desktop: "查看桌面", get_game_profile: "查询属性", get_game_clothes: "查询服装", get_task_catalog: "查询任务目录", get_current_task: "查询当前任务", get_task_queue: "查询任务队列", get_task_history: "查询任务历史", get_game_achievements: "查询成就", get_game_statistics: "查询统计", game_action: "游戏操作", jpet_settings: "JPet 设置", web_search: "网页搜索", bilibili_search: "B站搜索", open_url: "打开网页" };
  const suggestions = ["我现在有多少经验？", "有哪些任务可以做？", "搜一下今天的新闻", "把音量调到 30"];
  const format = value => JSON.stringify(value || {}, null, 2);

  $: query = search.trim().toLocaleLowerCase();
  $: visible = records.filter((item) => !query ||
    `${item.text || ""}\n${item.name || ""}\n${tools[item.name] || ""}\n${format(item.arguments)}\n${format(item.result)}`.toLocaleLowerCase().includes(query)
  ).sort((a, b) => a.id - b.id);
  $: replying = status.replying || sending;
  $: last = records.length ? records.reduce((a, b) => a.id > b.id ? a : b) : null;
  // Dots until the reply's first words arrive, or while a tool runs.
  $: waiting = replying && !query && (!last || last.role !== "assistant" || last.state !== "pending" || !last.text);
  $: statusError = status.state === "error" && status.message !== dismissedError ? status.message : "";

  function dateLabel(timestamp) {
    return new Date(timestamp * 1000).toLocaleString("zh-CN", {
      month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false,
    });
  }
  // A time divider before a message that follows a pause of five minutes or more.
  function showTime(index) {
    return index === 0 || visible[index].created_at - visible[index - 1].created_at >= 300;
  }

  function viewport() { return page?.closest(".panel-content"); }
  function nearBottom() {
    const view = viewport();
    return !view || view.scrollHeight - view.scrollTop - view.clientHeight < 120;
  }
  async function scrollToBottom(force = false) {
    if (!force && !stickToBottom) return;
    await tick();
    const view = viewport();
    if (view) view.scrollTop = view.scrollHeight;
  }
  function onScroll() { stickToBottom = nearBottom(); }

  async function refresh() {
    if (destroyed || refreshing) return;
    refreshing = true;
    controller = new AbortController();
    try {
      const [historyResponse, statusResponse] = await Promise.all([
        fetch("/api/voice/history", { cache: "no-store", signal: controller.signal }),
        fetch("/api/voice", { cache: "no-store", signal: controller.signal }),
      ]);
      if (!historyResponse.ok) throw new Error();
      const data = await historyResponse.json();
      if (!Array.isArray(data.list)) throw new Error();
      if (statusResponse.ok) status = await statusResponse.json();
      if (destroyed) return;
      const changed = JSON.stringify(data.list) !== JSON.stringify(records);
      records = data.list;
      limit = data.limit;
      saveError = data.error || "";
      error = "";
      if (changed) scrollToBottom(loading);
    } catch (failure) {
      if (!destroyed && failure.name !== "AbortError") error = "对话记录加载失败，请重试。";
    } finally {
      if (!destroyed) { loading = false; refreshing = false; schedule(); }
    }
  }
  // Poll quickly while Joi replies so streamed text appears as it arrives.
  function schedule() {
    clearTimeout(timer);
    if (!destroyed) timer = setTimeout(() => { if (document.hidden) schedule(); else refresh(); }, replying ? 400 : 1500);
  }

  async function send(text = draft) {
    const message = text.trim();
    if (!message || sending) return;
    sending = true;
    sendError = "";
    dismissedError = status.state === "error" ? status.message : dismissedError;
    try {
      const response = await fetch("/api/voice/chat", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ text: message }),
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(result.error || "消息发送失败，请重试");
      if (text === draft) draft = "";
      stickToBottom = true;
      status = { ...status, replying: true };
    } catch (failure) {
      sendError = failure.message || "消息发送失败，请重试";
    } finally {
      sending = false;
      await tick();
      resize();
      input?.focus();
      setTimeout(refresh, 80);
    }
  }
  async function stop() {
    try { await fetch("/api/voice/chat/stop", { method: "POST" }); } catch { /* The next refresh shows the state. */ }
    setTimeout(refresh, 80);
  }
  function onKeydown(event) {
    // Enter sends; Shift+Enter adds a line, and Enter that confirms an IME candidate does neither.
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing && event.keyCode !== 229) {
      event.preventDefault();
      send();
    }
  }
  function resize() {
    if (!input) return;
    input.style.height = "auto";
    input.style.height = `${Math.min(input.scrollHeight, 160)}px`;
  }

  onMount(() => {
    refresh();
    viewport()?.addEventListener("scroll", onScroll, { passive: true });
  });
  onDestroy(() => {
    destroyed = true;
    clearTimeout(timer);
    controller?.abort();
    viewport()?.removeEventListener("scroll", onScroll);
  });
</script>

<section class="chat-page" aria-label="AI 对话" bind:this={page}>
  <div class="chat-toolbar game-card">
    <span class="summary-icon"><UiIcon name="chat" size={24} /></span>
    <div class="summary-copy"><h2>和轴伊聊天</h2><p>打字或按 {status.shortcut || "Option"} 语音聊天，两种方式共用同一段对话。最近 {limit} 条消息保存在本机。</p></div>
    <div class="summary-actions">
      <button class="icon-button" class:active={searchOpen} aria-pressed={searchOpen} aria-label="搜索对话" title="搜索对话" on:click={() => { searchOpen = !searchOpen; if (!searchOpen) search = ""; }}><UiIcon name="search" size={17} /><span>搜索</span></button>
      <button class="icon-button voice" aria-haspopup="dialog" on:click={() => voiceSettingsOpen = true}><UiIcon name="settings" size={17} /><span>语音对话设置</span></button>
    </div>
  </div>

  {#if searchOpen}
    <label class="history-search"><span class="sr-only">搜索消息或工具调用</span><!-- svelte-ignore a11y-autofocus --><input type="search" bind:value={search} placeholder="搜索消息、工具或参数…" autofocus /></label>
    {#if query}<p class="result-count">找到 {visible.length} 条消息</p>{/if}
  {/if}

  {#if error || saveError}<div class="history-error" role="alert">{error || saveError}{#if error}<button on:click={refresh} disabled={refreshing}>重新加载</button>{/if}</div>{/if}

  <div class="chat-thread" aria-live="polite" aria-busy={replying}>
    {#if loading}
      <div class="history-empty" role="status"><UiIcon name="chat" size={34} /><h2>正在读取对话…</h2></div>
    {:else if error && records.length === 0}
      <div class="history-empty"><UiIcon name="chat" size={34} /><h2>暂时无法读取对话</h2><p>点击重新加载，再试一次。</p></div>
    {:else if records.length === 0 && !replying}
      <div class="history-empty">
        <span class="empty-avatar"><UiIcon name="spark" size={28} /></span>
        <h2>和轴伊说点什么吧</h2>
        <p>轴伊可以查询和安排任务、调整设置、上网搜索，或者只是陪你聊聊天。</p>
        <div class="suggestions">{#each suggestions as suggestion}<button on:click={() => send(suggestion)} disabled={sending}>{suggestion}</button>{/each}</div>
      </div>
    {:else if query && visible.length === 0}
      <div class="history-empty"><UiIcon name="chat" size={34} /><h2>没有找到相关对话</h2><p>试试其他关键词，或<button class="link" on:click={() => search = ""}>清空搜索</button>。</p></div>
    {:else}
      {#each visible as item, index (item.id)}
        {#if showTime(index)}<div class="time-divider"><time datetime={new Date(item.created_at * 1000).toISOString()}>{dateLabel(item.created_at)}</time></div>{/if}
        {#if item.role === "tool"}
          <div class="tool-row" class:failed={item.state === "failed" || item.result?.error}>
            <details>
              <summary>
                <UiIcon name="settings" size={14} />
                <strong>{tools[item.name] || "工具调用"}</strong>
                <span class="tool-state" class:pending={item.state === "pending"}>{item.state === "pending" ? "执行中…" : item.result?.error ? "失败" : states[item.state] || "已完成"}</span>
              </summary>
              <div class="tool-body">
                {#if item.result?.error}<p class="tool-error">{item.result.error}</p>
                {:else if item.state === "interrupted"}<p>已打断，未完成的操作可能已经生效。</p>
                {:else if item.result?.notice}<p>{item.result.notice}</p>{/if}
                <div class="tool-detail-label">{item.name} · 参数</div><pre>{format(item.arguments)}</pre>
                {#if item.result}<div class="tool-detail-label">结果</div><pre>{format(item.result)}</pre>{/if}
              </div>
            </details>
          </div>
        {:else}
          <article class="message-row" class:user-row={item.role === "user"} aria-label={item.role === "user" ? "你的消息" : "轴伊的消息"}>
            {#if item.role !== "user"}<span class="message-avatar"><UiIcon name="spark" size={16} /></span>{/if}
            <div class="message-content">
              <div class="message-bubble" class:streaming={item.role === "assistant" && item.state === "pending" && item.id === last?.id}>
                <p class:placeholder={!item.text}>{item.text || (item.role === "user" ? item.state === "pending" ? "正在识别语音…" : "未获取到语音转写" : item.state === "pending" ? "…" : item.state === "failed" ? "本次回复失败。" : "本次回复已停止。")}</p>
              </div>
              {#if (item.role === "user" && item.input !== "text") || item.state === "failed" || item.state === "interrupted"}
                <div class="message-meta">
                  {#if item.role === "user" && item.input !== "text"}<span class="meta-tag"><UiIcon name="mic" size={11} />语音转写</span>{/if}
                  {#if item.state === "failed"}<span class="meta-tag failed">失败</span>{:else if item.state === "interrupted"}<span class="meta-tag">已打断</span>{/if}
                </div>
              {/if}
            </div>
          </article>
        {/if}
      {/each}
      {#if waiting}
        <div class="message-row" role="status">
          <span class="message-avatar"><UiIcon name="spark" size={16} /></span>
          <div class="message-bubble typing"><span class="dots" aria-hidden="true"><i></i><i></i><i></i></span><span class="typing-label">{status.state === "tool" ? status.message || "正在执行工具…" : "轴伊正在思考…"}</span></div>
        </div>
      {/if}
    {/if}
  </div>

  <form class="composer" on:submit|preventDefault={() => send()}>
    {#if sendError || statusError}
      <div class="composer-error" role="alert">
        <span>{sendError || statusError}</span>
        {#if (sendError || statusError).includes("设置")}<button type="button" on:click={() => voiceSettingsOpen = true}>打开设置</button>{/if}
        <button type="button" aria-label="关闭提示" on:click={() => { sendError = ""; dismissedError = status.message; }}>×</button>
      </div>
    {/if}
    <div class="composer-box">
      <label class="sr-only" for="chat-input">给轴伊发消息</label>
      <textarea id="chat-input" bind:this={input} bind:value={draft} rows="1" maxlength="2000" placeholder="给轴伊发消息…" on:keydown={onKeydown} on:input={resize}></textarea>
      {#if replying && !draft.trim()}
        <button type="button" class="send-button stop" on:click={stop} aria-label="停止回复" title="停止回复"><span class="stop-square"></span></button>
      {:else}
        <button type="submit" class="send-button" disabled={!draft.trim() || sending} aria-label="发送" title="发送（Enter）"><UiIcon name="arrow" size={18} /></button>
      {/if}
    </div>
    <p class="composer-hint">Enter 发送 · Shift+Enter 换行{replying ? " · 发送新消息会打断当前回复" : ""}</p>
  </form>
</section>

<Modal title="语音对话设置" bind:open={voiceSettingsOpen} size="md">
  {#if voiceSettingsOpen}<VoiceSettings on:historycleared={() => { records = []; refresh(); }} />{/if}
</Modal>

<style>
  .chat-page { display: flex; flex-direction: column; }
  .chat-toolbar { display: flex; align-items: center; gap: 14px; padding: 16px 18px; margin-bottom: 14px; }
  .summary-icon { display: grid; place-items: center; flex-shrink: 0; width: 44px; height: 44px; color: var(--green-ink); background: var(--green-soft); border-radius: 13px; }
  .summary-copy { flex: 1; min-width: 0; }
  .summary-copy h2 { font-size: 15px; font-weight: 800; }
  .summary-copy p { margin-top: 4px; font-size: 11px; color: var(--muted); line-height: 1.7; }
  .summary-actions { display: flex; align-items: center; gap: 8px; }
  .icon-button { display: inline-flex; align-items: center; justify-content: center; gap: 6px; min-height: 38px; padding: 8px 12px; border: 1px solid #dce3d1; border-radius: 9px; background: #fff; color: var(--ink); font-size: 12px; font-weight: 800; white-space: nowrap; }
  .icon-button:hover, .icon-button.active { background: var(--green-soft); color: var(--green-ink); }
  .icon-button.voice { background: var(--green-soft); color: var(--green-ink); }
  .icon-button.voice:hover { background: #e6f0d6; }
  .history-search { display: block; margin-bottom: 10px; }
  .history-search input { width: 100%; min-height: 40px; padding: 9px 12px; border: 1px solid #dce3d1; border-radius: 9px; font-size: 12px; background: white; color: var(--ink); }
  .result-count { font-size: 11px; color: var(--muted); margin: -2px 0 10px; }
  .history-error { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 10px 14px; margin-bottom: 12px; border: 1px solid #efc7d1; border-radius: 10px; background: #fff3f5; color: #be5573; font-size: 12px; }
  .history-error button, .link { text-decoration: underline; font-weight: 700; }
  button:disabled { opacity: .55; cursor: default; }

  .chat-thread { flex: 1; display: flex; flex-direction: column; gap: 14px; min-height: 280px; padding: 20px 18px; border: 1px solid #e7ecde; border-radius: 14px; background: #fff; }
  .history-empty { display: flex; align-items: center; flex-direction: column; gap: 10px; margin: auto; padding: 32px 12px; text-align: center; color: #96b377; }
  .history-empty h2 { font-size: 16px; color: var(--ink); font-weight: 800; }
  .history-empty p { max-width: 400px; font-size: 12px; line-height: 1.9; color: var(--muted); }
  .empty-avatar { display: grid; place-items: center; width: 56px; height: 56px; border-radius: 18px; color: var(--green-ink); background: #ecf6dd; }
  .suggestions { display: flex; flex-wrap: wrap; justify-content: center; gap: 8px; margin-top: 6px; max-width: 460px; }
  .suggestions button { padding: 8px 12px; border: 1px solid #dce6cf; border-radius: 18px; background: #f8fbf2; color: var(--green-ink); font-size: 12px; font-weight: 700; }
  .suggestions button:hover { background: #ecf6dd; }

  .time-divider { display: flex; justify-content: center; }
  .time-divider time { padding: 2px 10px; border-radius: 10px; background: #f4f6ef; color: var(--muted); font-size: 10px; font-variant-numeric: tabular-nums; }
  .message-row { display: flex; align-items: flex-start; gap: 10px; max-width: 88%; }
  .message-row.user-row { align-self: flex-end; justify-content: flex-end; }
  .message-avatar { display: grid; place-items: center; width: 30px; height: 30px; flex-shrink: 0; color: var(--green-ink); background: #ecf6dd; border-radius: 10px; }
  .message-content { min-width: 0; display: flex; flex-direction: column; }
  .user-row .message-content { align-items: flex-end; }
  .message-bubble { padding: 10px 14px; border: 1px solid #e8eddc; border-radius: 4px 14px 14px; background: #f8fbf2; }
  .user-row .message-bubble { color: #fff; border-color: #6db51f; border-radius: 14px 4px 14px 14px; background: var(--green-gradient); text-shadow: 0 1px 0 #5c9a2433; }
  .message-bubble p { font-size: 13px; line-height: 1.85; white-space: pre-wrap; overflow-wrap: anywhere; user-select: text; }
  .message-bubble p.placeholder { color: var(--muted); font-size: 12px; }
  .user-row .message-bubble p.placeholder { color: #f2fbe6; }
  .message-bubble.streaming p::after { content: ""; display: inline-block; width: 6px; height: 14px; margin-left: 2px; vertical-align: -2px; border-radius: 2px; background: #9bcf5c; animation: blink 1s steps(2) infinite; }
  .message-meta { display: flex; gap: 6px; margin-top: 4px; }
  .meta-tag { display: inline-flex; align-items: center; gap: 3px; padding: 1px 6px; border-radius: 6px; background: #f2f4ec; color: var(--muted); font-size: 10px; }
  .meta-tag.failed { background: #fff0f4; color: #be5573; }
  .typing { display: flex; align-items: center; gap: 10px; color: var(--muted); font-size: 12px; }
  .dots { display: inline-flex; gap: 4px; }
  .dots i { width: 6px; height: 6px; border-radius: 50%; background: #9bcf5c; animation: bounce 1.2s infinite ease-in-out; }
  .dots i:nth-child(2) { animation-delay: .15s; }
  .dots i:nth-child(3) { animation-delay: .3s; }

  .tool-row { margin-left: 40px; max-width: calc(88% - 40px); }
  .tool-row details { border: 1px solid #e9e2f2; border-radius: 10px; background: #faf8fd; font-size: 11px; }
  .tool-row summary { display: flex; align-items: center; gap: 7px; padding: 6px 10px; cursor: pointer; color: #8e7eaa; list-style: none; }
  .tool-row summary::-webkit-details-marker { display: none; }
  .tool-row summary strong { color: var(--ink); font-weight: 700; }
  .tool-state { margin-left: auto; padding: 1px 6px; border-radius: 6px; background: #edf3e5; color: #7e9569; font-size: 10px; }
  .tool-state.pending { background: #fff2d8; color: #aa8138; }
  .tool-row.failed .tool-state { background: #fff0f4; color: #be5573; }
  .tool-body { padding: 0 10px 10px; }
  .tool-body p { font-size: 12px; line-height: 1.8; overflow-wrap: anywhere; }
  .tool-error { color: #be5573; }
  .tool-detail-label { margin-top: 8px; color: var(--muted); overflow-wrap: anywhere; }
  pre { margin-top: 4px; padding: 8px; max-height: 220px; overflow: auto; white-space: pre-wrap; overflow-wrap: anywhere; background: #fff; border: 1px solid #eee8f5; border-radius: 8px; user-select: text; }

  .composer { position: sticky; bottom: 0; z-index: 3; margin-top: 12px; padding: 10px 0 12px; background: linear-gradient(#f5f7ef00, #f5f7ef 14px); }
  .composer-box { display: flex; align-items: flex-end; gap: 8px; padding: 8px 8px 8px 14px; border: 1px solid #d9e3cc; border-radius: 14px; background: #fff; box-shadow: 0 3px 0 #e3e9d8, 0 6px 18px #66744312; }
  .composer-box:focus-within { border-color: #a0c46e; }
  textarea { flex: 1; min-width: 0; max-height: 160px; padding: 7px 0; border: 0; resize: none; background: transparent; color: var(--ink); font-size: 13px; line-height: 1.7; outline: none; box-shadow: none; }
  textarea:focus { box-shadow: none; }
  textarea::placeholder { color: #b6aaa0; }
  .send-button { display: grid; place-items: center; width: 38px; height: 38px; flex-shrink: 0; border-radius: 11px; color: #fff; border: 1px solid #67b320; background: var(--green-gradient); box-shadow: inset 0 1px 0 #ffffff66, 0 2px 0 #579e22; }
  .send-button:disabled { opacity: .45; }
  .send-button.stop { border-color: #dcc9bd; background: #fff; box-shadow: 0 2px 0 #e3dbd3; }
  .stop-square { width: 12px; height: 12px; border-radius: 3px; background: var(--ink); }
  .composer-hint { margin-top: 6px; padding-left: 4px; font-size: 10px; color: var(--muted); }
  .composer-error { display: flex; align-items: center; gap: 10px; margin-bottom: 8px; padding: 8px 12px; border: 1px solid #efc7d1; border-radius: 10px; background: #fff3f5; color: #be5573; font-size: 12px; }
  .composer-error span { flex: 1; min-width: 0; }
  .composer-error button { font-weight: 700; text-decoration: underline; }
  .composer-error button:last-child { text-decoration: none; font-size: 16px; line-height: 1; padding: 0 2px; }

  @keyframes bounce { 0%, 60%, 100% { transform: translateY(0); opacity: .5; } 30% { transform: translateY(-4px); opacity: 1; } }
  @keyframes blink { to { visibility: hidden; } }
  @media (prefers-reduced-motion: reduce) { .dots i, .message-bubble.streaming p::after { animation: none; } }
  @media (max-width: 560px) {
    .chat-toolbar { flex-wrap: wrap; padding: 14px; gap: 10px; }
    .summary-icon { width: 38px; height: 38px; }
    .summary-actions { width: 100%; }
    .summary-actions .icon-button { flex: 1; }
    .chat-thread { padding: 14px 10px; }
    .message-row { max-width: 94%; gap: 8px; }
    .tool-row { margin-left: 38px; max-width: calc(94% - 38px); }
  }
</style>
