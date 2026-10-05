<script>
  import { onMount, onDestroy } from "svelte";
  import AttributeIcon from "../components/AttributeIcon.svelte";
  import { Button, Tooltip } from "flowbite-svelte";
  import ClockIcon from "../assets/clock.svg";
  import ClothesIcon from "../assets/clothes.svg";
  import StarIcon from "../assets/star.png";
  import { sse } from "../sse.js";
  import { reportFrontendError } from "../logger.js";

  export let attributes = { exp: 0, speed: 0, endurance: 0, strength: 0, will: 0, intellect: 0 };
  export let online = false;
  let stateReceivedAt = 0;

  let currentTask = null;
  let taskList = [];
  let queue = [];
  let queueCapacity = 2;
  let queueUpgrade = null;
  let history = [];
  let timeRemain = 0;
  let busy = false;
  let loading = false;
  let error = "";
  let statusError = "";
  let requestVersion = 0;
  let clockTimer;
  let refreshTimer;
  let unsubscribe;

  function updateClock() {
    const elapsed = online && currentTask && !currentTask.paused ? (performance.now() - stateReceivedAt) / 1000 : 0;
    timeRemain = currentTask ? Math.max(0, currentTask.remaining_seconds - elapsed) : 0;
  }

  function applyState(data) {
    currentTask = data.current ?? null;
    stateReceivedAt = performance.now();
    taskList = data.list ?? [];
    queue = data.queue ?? [];
    queueCapacity = data.queue_capacity ?? 2;
    queueUpgrade = data.queue_upgrade ?? null;
    history = data.history ?? [];
    updateClock();
  }

  async function updateStatus() {
    if (busy || loading) return;
    loading = true;
    const version = ++requestVersion;
    try {
      const response = await fetch("/api/task");
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      if (version === requestVersion) {
        applyState(data);
        statusError = "";
      }
    } catch (failure) {
      if (version === requestVersion) statusError = "任务状态加载失败，正在重试。";
      reportFrontendError("[Task] status request failed", failure);
    } finally {
      loading = false;
    }
  }

  async function taskAction(url, method = "POST", body) {
    if (busy) return;
    busy = true;
    error = "";
    ++requestVersion;
    try {
      const response = await fetch(url, {
        method,
        ...(body ? { headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) } : {}),
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "任务操作失败，请重试。");
      applyState(data);
    } catch (failure) {
      error = failure.message || "任务操作失败，请重试。";
      reportFrontendError("[Task] action failed", failure);
    } finally {
      busy = false;
    }
  }

  function calcRate(task) { return task.rate ?? 0; }
  function calcCost(task) { return task.cost ?? 0; }

  function formatRemain(seconds) {
    const s = Math.max(0, Math.floor(seconds));
    const hours = Math.floor(s / 3600);
    const minutes = Math.floor((s % 3600) / 60);
    return `${hours ? `${hours}h` : ""}${minutes ? `${minutes}m` : ""}${s % 60 || (!hours && !minutes) ? `${s % 60}s` : ""}`;
  }

  function canQueue(task) {
    return online && queue.length < queueCapacity && task.status !== 3 &&
      (task.repeatable || (!queue.some((entry) => entry.id === task.id) && currentTask?.id !== task.id));
  }

  function submitTask(task) {
    const immediately = !currentTask && queue.length === 0 && calcRate(task) > 0;
    taskAction(`/api/task/${task.id}/${immediately ? "start" : "queue"}`);
  }

  $: queueBlocked = !currentTask && queue.length > 0 && calcRate(queue[0]) === 0;

  onMount(() => {
    updateStatus();
    clockTimer = setInterval(updateClock, 1000);
    refreshTimer = setInterval(updateStatus, 5000);
    unsubscribe = sse.subscribe((event) => {
      if (event?.data === "UPDATE" || event?.data === "TASK_COMPLETE") updateStatus();
    });
  });

  onDestroy(() => {
    clearInterval(clockTimer);
    clearInterval(refreshTimer);
    unsubscribe?.();
    ++requestVersion;
  });
</script>

<div>
  {#if error || statusError}
    <p class="mb-4 rounded border border-red-200 bg-red-50 p-3 text-sm text-red-600" role="alert">{error || statusError}</p>
  {/if}

  {#if currentTask}
    <div class="task mb-4">
      <div class="header">
        <span>{online ? "正在执行" : "已暂停"} · {currentTask.title}</span>
        <span class="text-sm">{timeRemain > 0 ? formatRemain(timeRemain) : online ? "正在结算…" : "已暂停"}</span>
      </div>
      <div class="content text-gray-600">
        <p class="mb-3 text-sm">{currentTask.desc}</p>
        <div class="mb-2 flex flex-wrap items-center gap-1">
          <span class="badge info">要求</span>
          {#each Object.entries(currentTask.requirements) as [key, value]}
            <AttributeIcon attribute={key} {value} fullfill={attributes[key] >= value} />
          {/each}
        </div>
        <div class="mb-2 flex flex-wrap items-center gap-1">
          <span class="badge warn">奖励</span>
          {#if currentTask.id === 1}
            <AttributeIcon attribute="exp" value={currentTask.rewards.exp} fullfill />
          {:else}
            {#each Object.entries(currentTask.rewards) as [key, value]}
              <AttributeIcon attribute={key} {value} fullfill />
            {/each}
          {/if}
          {#if currentTask.special}<span class="text-sm">{currentTask.special.desc}</span>{/if}
        </div>
        <div class="flex flex-wrap items-center justify-between gap-2">
          <span class="text-sm">成功率 {calcRate(currentTask)}% · 完成后自动结算</span>
          <span class="flex gap-2">
            {#if currentTask.repeatable}
              <Button color="alternative" size="xs" disabled={busy || !canQueue(currentTask)}
                on:click={() => taskAction(`/api/task/${currentTask.id}/queue`)}>再排一次</Button>
            {/if}
            <Button color="alternative" size="xs" disabled={busy || !online}
              on:click={() => taskAction(`/api/task/${currentTask.id}/cancel`)}>中止</Button>
          </span>
        </div>
      </div>
    </div>
  {/if}

  <div class="task mb-4">
    <div class="header"><span>待执行队列</span><span>{queue.length} / {queueCapacity}</span></div>
    <div class="content">
      <p class="mb-3 text-xs text-gray-500">基础 2 个位置，扩容依次消耗 1、2、5、10 颗星星，最多 6 个位置。运行中的任务不占位置。</p>
      {#if queueUpgrade}
        <div class="mb-3 flex flex-wrap items-center justify-between gap-2 rounded border border-lime-100 bg-lime-50 p-3">
          <span class="flex items-center gap-1 text-sm text-gray-600"><img src={StarIcon} class="h-4 w-4" alt="" />持有 {queueUpgrade.stars} 颗星星</span>
          {#if queueUpgrade.cost !== null}
            <Button color="alternative" size="xs" disabled={busy || !online || !queueUpgrade.available}
              on:click={() => taskAction("/api/task/queue/upgrade")}>解锁第 {queueCapacity + 1} 个位置 · {queueUpgrade.cost} 颗星星</Button>
            <p class="w-full text-xs text-gray-500">解锁后永久保留。{online && !queueUpgrade.available ? `星星不足，还需 ${queueUpgrade.cost - queueUpgrade.stars} 颗星星。` : ""}</p>
          {:else}
            <span class="text-sm text-lime-700">队列容量已全部解锁</span>
          {/if}
        </div>
      {/if}
      {#if queueBlocked}
        <p class="mb-3 rounded bg-amber-50 p-2 text-sm text-amber-700" role="status">
          队首任务成功率为 0，已暂停启动。请提升属性、调整顺序或移除该任务。
        </p>
      {/if}
      {#if queue.length === 0}
        <p class="py-2 text-sm text-gray-400">队列为空。可以从下方添加任务，排完后自动结束。</p>
      {:else}
        <ol class="divide-y divide-gray-100">
          {#each queue as task, index (task.entry_id)}
            <li class="flex flex-wrap items-center justify-between gap-2 py-3">
              <div class="flex items-center gap-2 text-gray-600">
                <span class="queue-position">{index + 1}</span>
                <div>
                  <p class="text-sm">{task.title}</p>
                  <p class="text-xs text-gray-400">预计 {formatRemain(calcCost(task))} · 当前成功率 {calcRate(task)}%</p>
                </div>
              </div>
              <div class="flex gap-1">
                <Button color="alternative" size="xs" aria-label={`上移${task.title}`} disabled={busy || !online || index === 0}
                  on:click={() => taskAction(`/api/task/queue/${task.entry_id}/move`, "POST", { direction: -1 })}>↑</Button>
                <Button color="alternative" size="xs" aria-label={`下移${task.title}`} disabled={busy || !online || index === queue.length - 1}
                  on:click={() => taskAction(`/api/task/queue/${task.entry_id}/move`, "POST", { direction: 1 })}>↓</Button>
                <Button color="alternative" size="xs" disabled={busy || !online}
                  on:click={() => taskAction(`/api/task/queue/${task.entry_id}`, "DELETE")}>移除</Button>
              </div>
            </li>
          {/each}
        </ol>
      {/if}
    </div>
  </div>

  {#if history.length > 0}
    <div class="task mb-4">
      <div class="header">最近结算</div>
      <div class="content">
        {#each history as task, index}
          <details class="history-item" open={index === 0}>
            <summary class="cursor-pointer text-sm text-gray-600">
              <span class:text-green-600={task.success} class:text-red-500={!task.success}>{task.success ? "成功" : "失败"}</span>
              · {task.title}
              <span class="ml-2 text-xs text-gray-400">{new Date(task.end_time * 1000).toLocaleString()}</span>
            </summary>
            <div class="mt-2 flex flex-wrap items-center gap-1 text-sm text-gray-500">
              {#if task.success}
                <span>已发放：</span>
                {#each Object.entries(task.rewards) as [key, value]}
                  <AttributeIcon attribute={key} {value} fullfill />
                {/each}
                {#if task.special}<span>{task.special.desc}</span>{/if}
              {:else}
                <span>本次未获得奖励。</span>
              {/if}
            </div>
          </details>
        {/each}
      </div>
    </div>
  {/if}

  <div class="task">
    <div class="header">任务列表</div>
    <div class="content">
      <ul class="divide-y divide-gray-100">
        {#each taskList as task (task.id)}
          <li class="py-3" class:archived={task.status === 3}>
            <div class="mb-2 flex items-center justify-between gap-2 text-gray-600">
              <div>
                <p class="text-sm"><span class="mr-2 text-green-600">T{task.id}</span>{task.title}</p>
                <p class="mt-1 text-xs text-gray-400"><img class="inline" width="12" height="12" src={ClockIcon} alt="" /> {formatRemain(calcCost(task))}</p>
              </div>
              {#if task.status === 3}
                <span class="text-xs text-gray-400">已完成</span>
              {:else}
                <Button color="alternative" size="sm" disabled={busy || !canQueue(task)} on:click={() => submitTask(task)}>
                  {!currentTask && queue.length === 0 && calcRate(task) > 0 ? "执行" : "加入队列"}
                </Button>
              {/if}
            </div>
            <div class="mb-2 flex flex-wrap items-center gap-1 text-gray-500">
              <span class="badge info">要求</span>
              {#each Object.entries(task.requirements) as [key, value]}
                <AttributeIcon attribute={key} {value} fullfill={attributes[key] >= value} />
              {/each}
            </div>
            {#if task.id === 1 || Object.keys(task.rewards).length > 0}
              <div class="mb-2 flex flex-wrap items-center gap-1 text-gray-500">
                <span class="badge warn">奖励</span>
                {#if task.id === 1}
                  <AttributeIcon attribute="exp" value={task.rewards.exp} fullfill />
                {:else}
                  {#each Object.entries(task.rewards) as [key, value]}
                    <AttributeIcon attribute={key} {value} fullfill />
                  {/each}
                {/if}
              </div>
            {/if}
            {#if task.special}
              <div class="mb-2 flex items-center gap-2 text-sm text-gray-500">
                <span class="badge warn">特殊奖励</span>
                <img class="inline" width="16" src={ClothesIcon} alt="" />{task.special.title}
                <Tooltip>{task.special.desc}</Tooltip>
              </div>
            {/if}
            {#if task.status !== 3}
              <p class="text-xs text-gray-500">成功率 {calcRate(task)}%{!task.repeatable && queue.some((entry) => entry.id === task.id) ? " · 已排队" : ""}</p>
            {/if}
          </li>
        {/each}
      </ul>
    </div>
  </div>
</div>

<style>
  .task { border-radius: 0.25rem; border: 1px solid #e2e8f0; background: white; box-shadow: 0 1px 2px rgb(0 0 0 / 5%); overflow: hidden; }
  .header { display: flex; align-items: center; justify-content: space-between; gap: 8px; color: white; background: #79ca2e; padding: 6px 12px; font-size: 1rem; font-weight: bold; }
  .content { padding: 12px; }
  .badge { padding: 4px 8px; border-radius: 4px; color: #fff; text-align: center; font-size: 12px; }
  .badge.info { background: #79ca2e; }
  .badge.warn { background: #ff666b; }
  .queue-position { display: inline-flex; align-items: center; justify-content: center; width: 24px; height: 24px; border-radius: 50%; background: #eef8e5; color: #579c1c; font-size: 12px; }
  .history-item { padding: 8px 0; }
  .history-item + .history-item { border-top: 1px solid #f3f4f6; }
  .archived { opacity: 0.5; }
</style>
