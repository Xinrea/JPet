<script>
  import { createEventDispatcher } from "svelte";
  export let updater = {};
  const dispatch = createEventDispatcher();
  let requesting = false;
  let actionError = "";
  $: busy = requesting || ["checking", "downloading", "verifying", "installing"].includes(updater.state);
  $: statusText = updater.state === "checking" ? "正在检查 GitHub Releases…"
    : updater.state === "downloading" ? `正在下载更新 · ${updater.progress || 0}%`
    : updater.state === "verifying" ? "正在校验并解压更新包…"
    : updater.state === "ready" ? "更新已下载，重启后安装。"
    : updater.state === "installing" ? "正在退出并安装更新，请稍候…"
    : updater.need_update ? "发现新版本" : updater.latest_version ? "当前已是最新版本" : "";
  async function updateAction(action) {
    requesting = true;
    actionError = "";
    try {
      const response = await fetch(`/api/update/${action}`, { method: "POST" });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "更新操作失败，请重试");
      updater = data;
      dispatch("refresh");
    } catch (error) {
      actionError = error.message || "无法连接 JPet，请稍后重试";
    } finally {
      requesting = false;
    }
  }
  function openDataFolder() {
    fetch("/api/config/folder", { method: "POST" });
  }
  function openlink(link) {
    fetch("/api/openlink", { method: "POST", body: JSON.stringify({link: link})});
  }
</script>

<div>
  <p class="font-medium text-lg">版本信息</p>
  <p class="font-medium text-sm">当前版本：{updater.local_version || "加载中"}</p>
  <p class="font-medium text-sm">最新版本：{updater.latest_version || "待检查"}</p>
  <div class="update-controls">
    <button disabled={busy || updater.state === "ready"} on:click={() => updateAction("check")}>检查更新</button>
    {#if updater.state === "ready"}
      <button class="primary" disabled={requesting} on:click={() => updateAction("install")}>重启并更新</button>
    {:else if updater.download_available}
      <button class="primary" disabled={busy} on:click={() => updateAction("download")}>下载新版本</button>
    {/if}
    <button class="release-link" on:click={() => openlink(updater.release_url || "https://github.com/Xinrea/JPet/releases")}>查看 Release</button>
  </div>
  {#if statusText}<p class="mt-2 text-sm" role="status" aria-live="polite">{statusText}</p>{/if}
  {#if updater.state === "downloading"}
    <progress class="update-progress" value={updater.progress || 0} max="100" aria-label="更新下载进度"></progress>
  {/if}
  {#if actionError || updater.error}<p class="mt-2 text-sm text-red-700" role="alert">{actionError || updater.error}</p>{/if}
  {#if updater.release_notes}
    <details class="mt-3 text-sm"><summary>更新说明</summary><p class="release-notes">{updater.release_notes}</p></details>
  {/if}
  <p class="font-medium text-lg mt-4">关于<b>经验值</b></p>
  <p class="font-medium text-sm">经验获取：1 经验 = 挂机 1 分钟。</p>
  <p class="font-medium text-sm">随着智力的提升，每分钟获取的经验值会逐渐增加，最多 500 点。</p>
  <p class="font-medium text-lg mt-4">关于<b>速度</b></p>
  <p class="font-medium text-sm">速度会缩短任务完成所需的时间，进行中的任务不受速度变化的影响。</p>
  <p class="font-medium text-lg mt-4">关于<b>任务队列</b></p>
  <p class="font-medium text-sm">任务结束后自动结算并发放奖励，随后执行队列中的下一项。每个排队任务只执行一次，队列为空时停止。</p>
  <p class="font-medium text-sm">初始可排 2 个待执行任务，每颗星增加 1 个位置，运行中的任务不占队列容量。队列可调整顺序、移除，并会在退出后保留。</p>
  <p class="font-medium text-sm">成功率为 0 的队首任务会等待属性提升，也可以将其他任务移到它前面。</p>
  <p class="font-medium text-lg mt-4">关于<b>毅力</b></p>
  <p class="font-medium text-sm">毅力会影响任务的成功率，当毅力为 0 的情况下，任务成功率最高为 70%；天有不测风云，任务成功率不会超过 95%。</p>
  <p class="font-medium text-lg mt-4">配置文件与游戏数据</p>
  <p class="font-medium text-sm">本程序配置文件与游戏数据均在本地存储，位于用户文档目录下，点击 <a class="underline decoration-green-500 decoration-2 font-bold" href={"#"} on:click={()=>openDataFolder()}>此处</a> 打开目录。</p>
  <p class="font-medium text-lg mt-4">动态与直播提醒</p>
  <p class="font-medium text-sm">由于 B 站风控机制的限制，添加太多监控目标会导致状态更新不及时，请尽量登录使用。</p>
  <p class="font-medium text-lg mt-4">轴芯等级</p>
  <p class="font-medium text-sm">轴伊直播间粉丝勋章等级。</p>
</div>

<style>
  .update-controls { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; margin-top: 12px; }
  .update-controls button { padding: 7px 12px; border: 1px solid #d1d5db; border-radius: 7px; background: white; font-size: 13px; }
  .update-controls button.primary { background: #4d7c0f; color: white; border-color: #4d7c0f; }
  .update-controls button.release-link { padding-inline: 4px; border-color: transparent; background: transparent; text-decoration: underline; color: #4d7c0f; }
  .update-controls button:disabled { opacity: .5; cursor: not-allowed; }
  .update-progress { display: block; width: 100%; height: 8px; margin-top: 10px; accent-color: #4d7c0f; }
  .release-notes { white-space: pre-wrap; overflow-wrap: anywhere; margin-top: 8px; font-weight: normal; }
</style>
