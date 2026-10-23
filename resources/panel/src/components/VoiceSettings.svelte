<script>
  import { onMount, onDestroy, createEventDispatcher } from "svelte";
  import { Input, Button, Select } from "flowbite-svelte";
  import SettingCard from "./SettingCard.svelte";
  import UiIcon from "./UiIcon.svelte";

  // Credentials are write-only; the API never returns a saved key.
  let voiceInputMode = "toggle";
  const voiceInputModes = [{ value: "toggle", name: "开关 · 全双工" }, { value: "hold", name: "按住 · 半双工" }];
  // Chat model (free text, optional OpenAI-compatible endpoint).
  let chatModel = "", chatBaseUrl = "", chatApiKey = "", chatHasKey = false, catalog = { chat_models: [] };
  let voiceApiKey = "", voiceWorkspace = "", voiceHasKey = false;
  let voiceLoading = true, voiceSaving = false, voiceError = "", voiceMessage = "";
  let voiceStatus = null, voiceTimer = null, voiceDisposed = false, voiceRefreshing = false;
  // Local speech recognition (SenseVoice) and synthesis (Qwen3-TTS) models, downloaded on demand.
  let modelBusy = false, modelError = "";
  $: speechModel = voiceStatus?.speech_model;
  const megabytes = value => (Number(value || 0) / 1048576).toFixed(0);
  async function speechModelAction(action) {
    modelBusy = true; modelError = "";
    try {
      const response = await fetch("/api/voice/speech-model", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action }) });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "模型操作失败");
      await refreshVoiceStatus();
    } catch (error) { modelError = error.message; } finally { modelBusy = false; }
  }
  async function refreshVoiceStatus() {
    if (voiceDisposed || voiceRefreshing || document.hidden) return;
    voiceRefreshing = true;
    try {
      const response = await fetch("/api/voice", { cache: "no-store" });
      if (!response.ok) return;
      const status = await response.json();
      if (!voiceDisposed) voiceStatus = status;
    } catch (_) { /* The next refresh retries a temporarily unavailable panel. */ }
    finally { voiceRefreshing = false; }
  }
  function applySettings(settings) {
    voiceInputMode = settings.input_mode || "toggle";
    voiceWorkspace = settings.workspace_id || "";
    voiceHasKey = !!settings.has_api_key;
    catalog = settings.catalog || catalog;
    chatModel = settings.chat_model || ""; chatBaseUrl = settings.chat_base_url || ""; chatHasKey = !!settings.has_chat_api_key;
  }
  async function loadVoiceSettings() {
    try {
      const response = await fetch("/api/config/voice", { cache: "no-store" });
      if (!response.ok) throw new Error("无法读取语音设置");
      const settings = await response.json();
      if (voiceDisposed) return;
      applySettings(settings);
      await refreshVoiceStatus();
    } catch (error) {
      if (!voiceDisposed) voiceError = error.message;
    } finally {
      if (!voiceDisposed) voiceLoading = false;
    }
  }
  async function saveVoiceSettings(clearKey = false, clearChatKey = false) {
    voiceSaving = true; voiceError = ""; voiceMessage = "";
    const payload = { workspace_id: voiceWorkspace.trim(), input_mode: voiceInputMode, chat_model: chatModel.trim(),
      chat_base_url: chatBaseUrl.trim() };
    if (clearKey) payload.clear_api_key = true;
    else if (voiceApiKey.trim()) payload.api_key = voiceApiKey.trim();
    if (clearChatKey) payload.clear_chat_api_key = true;
    else if (chatApiKey.trim()) payload.chat_api_key = chatApiKey.trim();
    try {
      const response = await fetch("/api/config/voice", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload),
      });
      const settings = await response.json();
      if (!response.ok) throw new Error(settings.error || "保存语音设置失败");
      if (voiceDisposed) return;
      applySettings(settings);
      voiceApiKey = ""; chatApiKey = "";
      voiceMessage = clearKey || clearChatKey ? "API Key 已移除" : "语音设置已保存";
      await refreshVoiceStatus();
    } catch (error) {
      if (!voiceDisposed) voiceError = error.message;
    } finally {
      if (!voiceDisposed) voiceSaving = false;
    }
  }

  const dispatch = createEventDispatcher();
  let historyConfirm = false, historyClearing = false, historyError = "", historyMessage = "";
  async function clearHistory() {
    historyClearing = true; historyError = ""; historyMessage = "";
    try {
      const response = await fetch("/api/voice/history", { method: "DELETE" });
      const result = await response.json().catch(() => ({}));
      dispatch("historycleared");
      if (!response.ok) throw new Error(result.error || "清空对话记录失败");
      if (!voiceDisposed) historyMessage = "对话记录已清空";
    } catch (error) {
      if (!voiceDisposed) historyError = error.message;
    } finally {
      if (!voiceDisposed) { historyClearing = false; historyConfirm = false; }
    }
  }

  onMount(() => {
    loadVoiceSettings();
    voiceTimer = setInterval(refreshVoiceStatus, 800);
  });
  onDestroy(() => {
    voiceDisposed = true;
    clearInterval(voiceTimer);
  });
</script>

<div class="voice-settings">
    <SettingCard title="语音对话" description="快捷键与本机语音模型" icon="mic">
      <div class="voice-shortcut"><UiIcon name="mic" size={23} /><div><strong>和轴伊聊一聊</strong><p>{#if voiceInputMode === "hold"}按住 <kbd>{voiceStatus?.shortcut || "Option / Ctrl"}</kbd> 打断回复并录音，松开后停止录音、播放回复。{:else}按 <kbd>{voiceStatus?.shortcut || "Option / Ctrl"}</kbd> 开启或关闭麦克风；开启后可连续对话，说话时可打断回复。{/if}</p></div></div>
      <div class="voice-tool">
        {#if speechModel?.state === "ready"}<strong>本机语音模型已就绪</strong><p class="setting-help">你的语音只在本机识别（SenseVoice），轴伊的声音在本机合成（Qwen3-TTS），都不会上传。模型约占 1.5 GB 磁盘，对话时约占 3 GB 内存，空闲两分钟后释放。</p>
          <Button size="xs" color="alternative" on:click={() => speechModelAction("remove")} disabled={modelBusy}>删除模型</Button>
        {:else if speechModel?.state === "downloading"}<strong>正在下载语音模型 {megabytes(speechModel.received)} / {megabytes(speechModel.total)} MB</strong>
          <Button size="xs" color="alternative" on:click={() => speechModelAction("cancel")} disabled={modelBusy}>取消</Button>
        {:else}<strong>需要先下载本机语音模型</strong><p class="setting-help">语音识别与轴伊的声音合成共约 1.5 GB，下载一次即可，都在本机运行。</p>
          {#if speechModel?.error}<p class="feedback error">{speechModel.error}</p>{/if}
          <Button size="xs" on:click={() => speechModelAction("download")} disabled={modelBusy}>下载模型</Button>
        {/if}
        {#if modelError}<p class="feedback error" role="alert">{modelError}</p>{/if}
      </div>
    </SettingCard>
    <SettingCard title="对话设置" description="快捷键方式和大模型" icon="settings">
      <div class="setting-field"><label for="voice-input-mode">Option / Ctrl 使用方式</label><Select id="voice-input-mode" items={voiceInputModes} bind:value={voiceInputMode} disabled={voiceLoading || voiceSaving} /></div>
      <div class="setting-field"><label for="voice-api-key">百炼 API Key<span>{voiceHasKey ? "已保存" : "待设置"}</span></label><Input id="voice-api-key" type="password" bind:value={voiceApiKey} placeholder={voiceHasKey ? "已保存；输入新 Key 可替换" : "sk-…"} autocomplete="off" spellcheck="false" maxlength={512} disabled={voiceLoading || voiceSaving} /></div>
      <div class="setting-field"><label for="voice-workspace">业务空间 ID</label><Input id="voice-workspace" bind:value={voiceWorkspace} placeholder="填写 API Key 所属的业务空间 ID" autocomplete="off" spellcheck="false" maxlength={63} disabled={voiceLoading || voiceSaving} /></div>
      <p class="setting-help">北京地域的 API Key 和业务空间，用于网页搜索、查看桌面，以及未填写接口地址时的大模型。API Key 保存在本机系统凭据存储中。</p>
      <div class="setting-field"><label for="chat-model">大模型</label><Input id="chat-model" list="chat-models" bind:value={chatModel} placeholder="qwen-plus" autocomplete="off" spellcheck="false" maxlength={128} disabled={voiceLoading || voiceSaving} />
        <datalist id="chat-models">{#each catalog.chat_models as model}<option value={model.id}>{model.name}</option>{/each}</datalist></div>
      <div class="setting-field"><label for="chat-base-url">大模型接口地址<span>可选</span></label><Input id="chat-base-url" bind:value={chatBaseUrl} placeholder="留空使用百炼；或填写 OpenAI 兼容的 Base URL" autocomplete="off" spellcheck="false" maxlength={512} disabled={voiceLoading || voiceSaving} /></div>
      {#if chatBaseUrl.trim()}
      <div class="setting-field"><label for="chat-api-key">大模型 API Key<span>{chatHasKey ? "已保存" : "待设置"}</span></label><Input id="chat-api-key" type="password" bind:value={chatApiKey} placeholder={chatHasKey ? "已保存；输入新 Key 可替换" : "该接口的 API Key"} autocomplete="off" spellcheck="false" maxlength={512} disabled={voiceLoading || voiceSaving} /></div>
      {/if}
      <p class="setting-help">保存后下一次对话生效。</p>
      <div class="setting-actions"><Button size="sm" on:click={() => saveVoiceSettings()} disabled={voiceLoading || voiceSaving}>{voiceSaving ? "保存中…" : "保存语音设置"}</Button>{#if voiceHasKey}<Button size="sm" color="alternative" on:click={() => saveVoiceSettings(true)} disabled={voiceLoading || voiceSaving}>移除 Key</Button>{/if}{#if chatHasKey}<Button size="sm" color="alternative" on:click={() => saveVoiceSettings(false, true)} disabled={voiceLoading || voiceSaving}>移除大模型 Key</Button>{/if}</div>
      {#if voiceError}<p class="feedback error" role="alert">{voiceError}</p>{/if}
      {#if voiceMessage}<p class="feedback success" role="status">{voiceMessage}</p>{/if}
    </SettingCard>
    <SettingCard title="对话与工具状态" description="查看最近的回复和执行结果" icon="spark">
      <p class="setting-help">可调整声音、显示、桌面互动、通知、轮盘和装扮，查看桌面、查询游戏数据、安排或取消任务、升级属性、搜索网页和 B 站，以及用默认浏览器打开网页。B 站搜索使用当前登录账号；查看桌面时会截图并发送给百炼视觉模型，Mac 首次使用需允许录屏权限。</p>
      {#if voiceStatus?.message}<p class="feedback" class:error={voiceStatus.state === "error"} aria-live="polite">{voiceStatus.message}</p>{/if}
      {#if voiceStatus?.reply}<p class="voice-reply">{voiceStatus.reply}</p>{/if}
      {#if voiceStatus?.last_tool}
        <div class="voice-tool"><strong>{voiceStatus.last_tool.label} · {voiceStatus.last_tool.ok ? "完成" : "失败"}</strong>
          {#if voiceStatus.last_tool.error}<p class="feedback error">{voiceStatus.last_tool.error}</p>{/if}
          {#if voiceStatus.last_tool.url}<p><a href={voiceStatus.last_tool.url} target="_blank" rel="noopener noreferrer">{voiceStatus.last_tool.url}</a></p>{/if}
          {#each (voiceStatus.last_tool.sources || voiceStatus.last_tool.results || []) as source}{#if source.url}<p><a href={source.url} target="_blank" rel="noopener noreferrer">{source.title || source.url}</a></p>{/if}{/each}
        </div>
      {/if}
    </SettingCard>
    <SettingCard title="对话记录" description="管理保存在本机的聊天内容" icon="chat">
      <p class="setting-help">清空后，本机保存的对话文字和工具调用摘要将被删除，且无法恢复。当前语音会话也会断开，下次对话时轴伊不会记得之前的内容。</p>
      <div class="setting-actions">
        {#if historyConfirm}
          <Button size="sm" color="red" on:click={clearHistory} disabled={historyClearing}>{historyClearing ? "清空中…" : "确认清空"}</Button>
          <Button size="sm" color="alternative" on:click={() => historyConfirm = false} disabled={historyClearing}>取消</Button>
        {:else}
          <Button size="sm" color="alternative" on:click={() => { historyConfirm = true; historyMessage = ""; historyError = ""; }}>清空对话记录</Button>
        {/if}
      </div>
      {#if historyError}<p class="feedback error" role="alert">{historyError}</p>{/if}
      {#if historyMessage}<p class="feedback success" role="status">{historyMessage}</p>{/if}
    </SettingCard>
</div>

<style>
  .voice-settings { display: flex; flex-direction: column; gap: 16px; }
</style>
