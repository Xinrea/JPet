<script>
  import { onMount, onDestroy, createEventDispatcher } from "svelte";
  import { Input, Button, Select } from "flowbite-svelte";
  import SettingCard from "./SettingCard.svelte";
  import UiIcon from "./UiIcon.svelte";

  // Credentials are write-only; the API never returns a saved key.
  let voiceInputMode = "toggle", savedInputMode = "toggle";
  const voiceInputModes = [{ value: "toggle", name: "按一下" }, { value: "hold", name: "按住" }];
  // Microphone and speaker; "" is the system default. A saved device that is
  // not connected stays selectable, and the default is used until it returns.
  let inputDevice = "", outputDevice = "", savedInputDevice = "", savedOutputDevice = "";
  let audioDevices = { inputs: [], outputs: [] }, devicesRefreshing = false;
  // Shortcut mode and devices save as soon as they change, apart from the chat settings form.
  let audioSaving = false, audioError = "";
  async function saveAudioSettings() {
    audioSaving = true; audioError = "";
    try {
      const response = await fetch("/api/config/voice", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ input_mode: voiceInputMode, input_device: inputDevice, output_device: outputDevice }),
      });
      const settings = await response.json();
      if (!response.ok) throw new Error(settings.error || "保存失败");
      if (voiceDisposed) return;
      applyAudioSettings(settings);
      await refreshVoiceStatus();
    } catch (error) {
      if (voiceDisposed) return;
      audioError = error.message;
      voiceInputMode = savedInputMode; inputDevice = savedInputDevice; outputDevice = savedOutputDevice;
    } finally {
      if (!voiceDisposed) audioSaving = false;
    }
  }
  const deviceItems = (devices, saved) => [{ value: "", name: "系统默认" },
    ...devices.map(device => ({ value: device.id, name: device.name || device.id })),
    ...(saved && !devices.some(device => device.id === saved) ? [{ value: saved, name: "已选择的设备（未连接，暂用系统默认）" }] : [])];
  $: inputItems = deviceItems(audioDevices.inputs, savedInputDevice);
  $: outputItems = deviceItems(audioDevices.outputs, savedOutputDevice);
  async function refreshDevices() {
    devicesRefreshing = true;
    try {
      const response = await fetch("/api/config/voice", { cache: "no-store" });
      const settings = await response.json();
      if (response.ok && !voiceDisposed) audioDevices = settings.audio_devices || audioDevices;
    } catch (_) { /* Keep the current list. */ } finally { devicesRefreshing = false; }
  }
  // Chat model (free text, optional OpenAI-compatible endpoint).
  let chatModel = "", chatBaseUrl = "", chatApiKey = "", chatHasKey = false, catalog = { chat_models: [] };
  let voiceApiKey = "", voiceWorkspace = "", voiceHasKey = false;
  let voiceLoading = true, voiceSaving = false, voiceError = "", voiceMessage = "";
  let voiceStatus = null, voiceTimer = null, voiceDisposed = false, voiceRefreshing = false;
  // Local speech recognition (SenseVoice) and synthesis (Qwen3-TTS) models, downloaded on demand.
  let modelBusy = false, modelError = "";
  $: speechModel = voiceStatus?.speech_model;
  const megabytes = value => (Number(value || 0) / 1048576).toFixed(0);
  $: modelPercent = speechModel?.total > 0 ? Math.min(100, Math.floor(100 * speechModel.received / speechModel.total)) : 0;
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
  function applyAudioSettings(settings) {
    voiceInputMode = savedInputMode = settings.input_mode || "toggle";
    inputDevice = savedInputDevice = settings.input_device || "";
    outputDevice = savedOutputDevice = settings.output_device || "";
    audioDevices = settings.audio_devices || audioDevices;
  }
  function applySettings(settings) {
    applyAudioSettings(settings);
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
    const payload = { workspace_id: voiceWorkspace.trim(), chat_model: chatModel.trim(), chat_base_url: chatBaseUrl.trim() };
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
    <SettingCard title="语音对话" description="快捷键、声音设备与本机语音模型" icon="mic">
      <div class="voice-shortcut"><UiIcon name="mic" size={23} /><div><strong>和轴伊聊一聊</strong>
        <p><select class="mode-select" aria-label="快捷键使用方式" bind:value={voiceInputMode} on:change={saveAudioSettings} disabled={voiceLoading || audioSaving}>{#each voiceInputModes as mode}<option value={mode.value}>{mode.name}</option>{/each}</select>
          <kbd>{voiceStatus?.shortcut || "Option / Ctrl"}</kbd> {#if voiceInputMode === "hold"}打断回复并录音，松开后停止录音、播放回复。{:else}开启或关闭麦克风；开启后可连续对话，说话时可打断回复。{/if}</p></div></div>
      <div class="device-row">
        <div class="setting-field"><label for="voice-input-device">麦克风</label><Select id="voice-input-device" items={inputItems} bind:value={inputDevice} on:change={saveAudioSettings} placeholder="" disabled={voiceLoading || audioSaving} /></div>
        <div class="setting-field"><label for="voice-output-device">扬声器</label><Select id="voice-output-device" items={outputItems} bind:value={outputDevice} on:change={saveAudioSettings} placeholder="" disabled={voiceLoading || audioSaving} /></div>
      </div>
      <p class="setting-help">轴伊的回复从所选扬声器播放。插拔设备后可<button type="button" class="link-button" on:click={refreshDevices} disabled={devicesRefreshing}>{devicesRefreshing ? "刷新中…" : "刷新设备列表"}</button>；所选设备未连接时暂用系统默认。</p>
      {#if audioError}<p class="feedback error" role="alert">{audioError}</p>{/if}
      <div class="voice-tool">
        {#if speechModel?.state === "ready"}<strong>本机语音模型已就绪</strong><p class="setting-help">你的语音只在本机识别（SenseVoice），轴伊的声音在本机合成（Qwen3-TTS），都不会上传。模型约占 1.5 GB 磁盘，对话时约占 3 GB 内存，空闲两分钟后释放。</p>
          <Button size="xs" color="alternative" on:click={() => speechModelAction("remove")} disabled={modelBusy}>删除模型</Button>
        {:else if speechModel?.state === "downloading"}<strong>正在下载语音模型 · {modelPercent}%</strong>
          <div class="model-progress-row">
            <progress class="model-progress" value={speechModel.received || 0} max={speechModel.total || 1} aria-label="语音模型下载进度"></progress>
            <Button size="xs" color="alternative" on:click={() => speechModelAction("cancel")} disabled={modelBusy}>取消</Button>
          </div>
          <p class="setting-help">{megabytes(speechModel.received)} / {megabytes(speechModel.total)} MB</p>
        {:else}<strong>需要先下载本机语音模型</strong><p class="setting-help">语音识别与轴伊的声音合成共约 1.5 GB，下载一次即可，都在本机运行。</p>
          {#if speechModel?.error}<p class="feedback error">{speechModel.error}</p>{/if}
          <Button size="xs" on:click={() => speechModelAction("download")} disabled={modelBusy}>下载模型</Button>
        {/if}
        {#if modelError}<p class="feedback error" role="alert">{modelError}</p>{/if}
      </div>
    </SettingCard>
    <SettingCard title="对话设置" description="百炼 API Key 和大模型" icon="settings">
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
  .link-button { color: var(--green-ink); text-decoration: underline; padding: 0 2px; }
  .link-button:disabled { color: var(--muted); text-decoration: none; }
  .model-progress-row { display: flex; align-items: center; gap: 10px; margin: 4px 0 2px; }
  .model-progress { display: block; flex: 1; min-width: 0; height: 8px; border: 0; border-radius: 8px; overflow: hidden; background: #edf2e4; }
  .model-progress::-webkit-progress-bar { background: #edf2e4; border-radius: 8px; }
  .model-progress::-webkit-progress-value { background: var(--green-gradient); border-radius: 8px; transition: width .3s ease; }
  .model-progress::-moz-progress-bar { background: #8dce38; border-radius: 8px; }
  .device-row { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; margin-top: 4px; }
  @media (max-width: 420px) { .device-row { grid-template-columns: 1fr; gap: 0; } }
  /* A compact dropdown that reads as part of the shortcut sentence. */
  .voice-shortcut .mode-select { display: inline-block; min-height: 0; height: 22px; padding: 0 20px 0 8px; margin-right: 2px; vertical-align: 1px;
    font-size: 11px; font-weight: 800; line-height: 20px; color: var(--green-ink); cursor: pointer;
    border: 1px solid #cfe2b7; border-radius: 6px; background-color: #f4f9ea; box-shadow: 0 1px 0 #d8e7c6;
    appearance: none; -webkit-appearance: none; background-repeat: no-repeat; background-position: right 6px center; background-size: 8px 8px;
    background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 6'%3E%3Cpath d='M1 1l4 4 4-4' fill='none' stroke='%23578a27' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/%3E%3C/svg%3E"); }
  .voice-shortcut .mode-select:hover:not(:disabled) { border-color: #a0c46e; }
  .voice-shortcut .mode-select:disabled { cursor: default; opacity: .7; }
</style>
