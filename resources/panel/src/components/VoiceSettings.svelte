<script>
  import { onMount, onDestroy } from "svelte";
  import { Input, Button, Select } from "flowbite-svelte";
  import SettingCard from "./SettingCard.svelte";
  import UiIcon from "./UiIcon.svelte";

  // Voice credentials are write-only; the API never returns the saved key.
  let voiceProvider = "custom";
  let voiceInputMode = "toggle";
  const voiceInputModes = [{ value: "toggle", name: "开关 · 全双工" }, { value: "hold", name: "按住 · 半双工" }];
  const voiceProviders = [{ value: "custom", name: "自定义千问服务" }, { value: "jpet", name: "JPet Server · PowerLive 账号" }];
  let powerlive = null, powerliveTimer = null, powerliveBusy = false, powerliveError = "", powerliveRefreshing = false;
  async function refreshPowerlive() {
    if (voiceDisposed || powerliveRefreshing || document.hidden) return;
    powerliveRefreshing = true;
    try { const response = await fetch("/api/powerlive", { cache: "no-store" }); if (response.ok && !voiceDisposed) powerlive = await response.json(); }
    catch (_) { /* Retry when the panel reconnects. */ } finally { powerliveRefreshing = false; }
  }
  async function powerliveAction(action) {
    powerliveBusy = true; powerliveError = "";
    try {
      const response = await fetch(`/api/powerlive/${action}`, { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
      const result = await response.json(); if (!response.ok) throw new Error(result.error || "账号操作失败");
      await refreshPowerlive();
    } catch (error) { powerliveError = error.message; } finally { powerliveBusy = false; }
  }
  const tokens = value => Number(value || 0).toLocaleString("zh-CN");
  let voiceApiKey = "", voiceWorkspace = "", voiceHasKey = false;
  let voiceLoading = true, voiceSaving = false, voiceError = "", voiceMessage = "";
  let voiceStatus = null, voiceTimer = null, voiceDisposed = false, voiceRefreshing = false;
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
  async function loadVoiceSettings() {
    try {
      const response = await fetch("/api/config/voice", { cache: "no-store" });
      if (!response.ok) throw new Error("无法读取语音设置");
      const settings = await response.json();
      if (voiceDisposed) return;
      voiceProvider = settings.provider || "custom";
      voiceInputMode = settings.input_mode || "toggle";
      voiceWorkspace = settings.workspace_id || "";
      voiceHasKey = settings.has_api_key;
      await refreshVoiceStatus();
    } catch (error) {
      if (!voiceDisposed) voiceError = error.message;
    } finally {
      if (!voiceDisposed) voiceLoading = false;
    }
  }
  async function saveVoiceSettings(clearKey = false) {
    voiceSaving = true; voiceError = ""; voiceMessage = "";
    const payload = { workspace_id: voiceWorkspace.trim(), provider: voiceProvider, input_mode: voiceInputMode };
    if (clearKey) payload.clear_api_key = true;
    else if (voiceApiKey.trim()) payload.api_key = voiceApiKey.trim();
    try {
      const response = await fetch("/api/config/voice", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload),
      });
      const settings = await response.json();
      if (!response.ok) throw new Error(settings.error || "保存语音设置失败");
      if (voiceDisposed) return;
      voiceProvider = settings.provider;
      voiceInputMode = settings.input_mode || "toggle";
      voiceWorkspace = settings.workspace_id;
      voiceHasKey = settings.has_api_key;
      voiceApiKey = "";
      voiceMessage = clearKey ? "API Key 已移除" : "语音设置已保存";
      await refreshVoiceStatus();
    } catch (error) {
      if (!voiceDisposed) voiceError = error.message;
    } finally {
      if (!voiceDisposed) voiceSaving = false;
    }
  }

  onMount(() => {
    loadVoiceSettings();
    refreshPowerlive();
    powerliveTimer = setInterval(refreshPowerlive, 2000);
    voiceTimer = setInterval(refreshVoiceStatus, 800);
  });
  onDestroy(() => {
    voiceDisposed = true;
    clearInterval(voiceTimer);
    clearInterval(powerliveTimer);
  });
</script>

<div class="voice-settings">
    <SettingCard title="语音对话" description="选择快捷键的使用方式" icon="mic">
      <div class="voice-shortcut"><UiIcon name="mic" size={23} /><div><strong>和轴伊聊一聊</strong><p>{#if voiceInputMode === "hold"}按住 <kbd>{voiceStatus?.shortcut || "Option / Ctrl"}</kbd> 打断回复并录音，松开后停止录音、播放回复。{:else}按 <kbd>{voiceStatus?.shortcut || "Option / Ctrl"}</kbd> 开启或关闭麦克风；开启后可连续对话，说话时可打断回复。{/if}</p></div></div>
      <div class="setting-field"><label for="voice-input-mode">Option / Ctrl 使用方式</label><Select id="voice-input-mode" items={voiceInputModes} bind:value={voiceInputMode} disabled={voiceLoading || voiceSaving} /></div>
      <div class="setting-field"><label for="voice-provider">AI 服务方式</label><Select id="voice-provider" items={voiceProviders} bind:value={voiceProvider} disabled={voiceLoading || voiceSaving} /></div>
      {#if voiceProvider === "custom"}
      <div class="setting-field"><label for="voice-api-key">百炼 API Key<span>{voiceHasKey ? "已保存" : "待设置"}</span></label><Input id="voice-api-key" type="password" bind:value={voiceApiKey} placeholder={voiceHasKey ? "已保存；输入新 Key 可替换" : "sk-…"} autocomplete="off" spellcheck="false" maxlength={512} disabled={voiceLoading || voiceSaving} /></div>
      <div class="setting-field"><label for="voice-workspace">业务空间 ID</label><Input id="voice-workspace" bind:value={voiceWorkspace} placeholder="填写 API Key 所属的业务空间 ID" autocomplete="off" spellcheck="false" maxlength={63} disabled={voiceLoading || voiceSaving} /></div>
      <p class="setting-help">使用北京地域的 API Key 和业务空间。API Key 保存在本机系统凭据存储中。</p>
      {:else}
        <p class="setting-help">使用 PowerLive 账号登录，共用 JPet Server 提供的千问服务。</p>
        {#if powerlive?.logged_in}
          <p><strong>{powerlive.user?.name || powerlive.user?.email || "PowerLive 账号"}</strong>{#if powerlive.user?.email}<br/><span class="setting-help">{powerlive.user.email}</span>{/if}</p>
          {#if powerlive.quota}
            <p class="setting-help">今日已用 {tokens(powerlive.quota.used)} / {tokens(powerlive.quota.limit)} token<br/>可用 {tokens(powerlive.quota.remaining)} · 请求中预留 {tokens(powerlive.quota.reserved)}<br/>北京时间每日 00:00 重置，语音、桌面识别和网页搜索共用额度。</p>
          {/if}
          {#if !powerlive.service_ready}<p class="feedback error">暂时无法连接 JPet AI 服务，请稍后重试。</p>{/if}
          <Button color="alternative" size="sm" on:click={() => powerliveAction("logout")} disabled={powerliveBusy}>退出 PowerLive</Button>
        {:else}
          <Button size="sm" on:click={() => powerliveAction("login")} disabled={powerliveBusy || powerlive?.refreshing}>{powerlive?.pending ? "重新打开登录页面" : "使用 PowerLive 登录"}</Button>
          {#if powerlive?.pending}<p class="setting-help" role="status">请在系统浏览器中完成登录，完成后会自动更新此处。</p>{/if}
          {#if powerlive?.refreshing}<p class="setting-help" role="status">正在恢复登录…</p>{/if}
        {/if}
        {#if powerliveError || powerlive?.error}<p class="feedback error" role="alert">{powerliveError || powerlive.error}</p>{/if}
      {/if}
      <div class="setting-actions"><Button size="sm" on:click={() => saveVoiceSettings()} disabled={voiceLoading || voiceSaving}>{voiceSaving ? "保存中…" : "保存语音设置"}</Button>{#if voiceProvider === "custom" && voiceHasKey}<Button size="sm" color="alternative" on:click={() => saveVoiceSettings(true)} disabled={voiceLoading || voiceSaving}>移除 Key</Button>{/if}</div>
      {#if voiceError}<p class="feedback error" role="alert">{voiceError}</p>{/if}
      {#if voiceMessage}<p class="feedback success" role="status">{voiceMessage}</p>{/if}
    </SettingCard>
    <SettingCard title="对话与工具状态" description="查看最近的回复和执行结果" icon="spark">
      <p class="setting-help">可调整声音、显示、桌面互动、通知、轮盘和装扮，查看桌面、查询游戏数据、安排或取消任务、升级属性、搜索网页和 B 站，以及用默认浏览器打开网页。B 站搜索使用当前登录账号；查看桌面时会截图并发送给千问，Mac 首次使用需允许录屏权限。</p>
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
</div>

<style>
  .voice-settings { display: flex; flex-direction: column; gap: 16px; }
</style>
