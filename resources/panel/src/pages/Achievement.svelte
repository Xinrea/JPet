<script>
  import { onMount, onDestroy } from "svelte";
  import { sse } from "../sse.js";
  import { reportFrontendError } from "../logger.js";
  import AchievementIcon from "../components/AchievementIcon.svelte";

  let achievements = [];
  let total = 50;
  let unlocked = 0;
  let loading = true;
  let error = "";
  let category = "全部";
  let status = "all";
  let search = "";
  let order = "default";
  let timer;
  let unsubscribe;
  let controller;
  let fetching = false;
  let pending = false;
  let destroyed = false;
  const categories = ["全部", "陪伴", "互动", "成长", "任务", "衣装", "升星"];

  $: percent = total ? Math.round(unlocked / total * 100) : 0;
  $: counts = Object.fromEntries(categories.map((name) => [name,
    achievements.filter((item) => name === "全部" || item.category === name).length]));
  $: visible = achievements.filter((item) =>
    (category === "全部" || item.category === category) &&
    (status === "all" || (status === "unlocked" ? item.unlocked : !item.unlocked)) &&
    `${item.title} ${item.description}`.includes(search.trim())
  ).sort((a, b) => {
    if (order === "recent") return b.unlocked_at - a.unlocked_at;
    if (order === "progress") return Number(a.unlocked) - Number(b.unlocked) || b.progress / b.target - a.progress / a.target;
    return 0;
  });

  function dateLabel(timestamp) {
    return new Date(timestamp * 1000).toLocaleDateString("zh-CN", { year: "numeric", month: "2-digit", day: "2-digit" });
  }

  async function refresh() {
    if (destroyed) return;
    if (fetching) { pending = true; return; }
    fetching = true;
    controller = new AbortController();
    try {
      const response = await fetch("/api/achievements", { signal: controller.signal });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      if (!Array.isArray(data.list)) throw new Error("Invalid achievement response");
      if (destroyed) return;
      achievements = data.list;
      total = data.total;
      unlocked = data.unlocked;
      error = "";
    } catch (failure) {
      if (failure.name !== "AbortError") {
        error = "成就加载失败，请重试。";
        reportFrontendError("[Achievement] load failed", failure);
      }
    } finally {
      loading = false;
      fetching = false;
      if (pending && !destroyed) { pending = false; refresh(); }
    }
  }

  onMount(() => {
    refresh();
    timer = setInterval(refresh, 30000);
    unsubscribe = sse.subscribe((event) => {
      if (event?.data === "UPDATE" || event?.data?.includes('"ACHIEVEMENT_UNLOCKED"')) refresh();
    });
  });
  onDestroy(() => {
    destroyed = true;
    clearInterval(timer);
    unsubscribe?.();
    controller?.abort();
  });
</script>

<section class="achievement-page" aria-label="成就收藏">
  <div class="collection">
    <div class="collection-top">
      <div>
        <h1><span class="title-icon"><AchievementIcon name="trophy" size={21} /></span> 成就收藏</h1>
        <p>记录和轴伊一起成长的每一步</p>
      </div>
      <div class="collection-count"><strong>{unlocked}</strong><span> / {total}</span></div>
    </div>
    <div class="progress-label"><span>已收集 {unlocked} 项 · 还有 {total - unlocked} 项待解锁</span><strong>{percent}%</strong></div>
    <progress class="collection-progress" value={unlocked} max={total} aria-label="成就总收集进度"></progress>
    {#if unlocked === total && total > 0}<p class="complete"><AchievementIcon name="laurel" size={18} /> 全部成就已收集，感谢一路相伴！</p>{/if}
  </div>

  {#if error}
    <div class="error" role="alert">{error}<button on:click={refresh} disabled={fetching}>重新加载</button></div>
  {/if}
  {#if loading}
    <p class="empty" role="status">正在打开成就收藏册…</p>
  {:else}
    <div class="categories" aria-label="成就分类">
      {#each categories as name}
        <button class:selected={category === name} aria-pressed={category === name} on:click={() => category = name}>{name}<span class="category-count">{counts[name]}</span></button>
      {/each}
    </div>
    <div class="tools">
      <label class="search"><span class="sr-only">搜索成就名称或解锁条件</span><input type="search" bind:value={search} placeholder="搜索成就或解锁条件" /></label>
      <div class="filter-row">
        <div class="status-tabs" aria-label="解锁状态">
          {#each [{value: "all", label: "全部"}, {value: "locked", label: "未解锁"}, {value: "unlocked", label: "已解锁"}] as option}
            <button class:selected={status === option.value} aria-pressed={status === option.value} on:click={() => status = option.value}>{option.label}</button>
          {/each}
        </div>
        <label class="sort"><span class="sr-only">排序方式</span><select bind:value={order}><option value="default">默认排序</option><option value="progress">即将解锁</option><option value="recent">最近解锁</option></select></label>
      </div>
    </div>
    <div class="result-count" aria-live="polite">显示 {visible.length} 项成就</div>
    <div class="cards">
      {#each visible as item (item.id)}
        <article class="card" class:unlocked={item.unlocked}>
          <div class="badge"><AchievementIcon name={item.icon} /></div>
          <div class="card-content">
            <div class="card-heading">
              <h2>{item.title}<span class="category-label">{item.category}</span></h2>
              <span class="state">{#if item.unlocked}<AchievementIcon name="check" size={12} />{/if}{item.unlocked ? "已解锁" : "未解锁"}</span>
            </div>
            <p class="condition">{item.description}</p>
            <div class="card-bottom">
              {#if item.unlocked}
                <span class="date">解锁于 {dateLabel(item.unlocked_at)}</span>
              {:else}
                <progress value={item.progress} max={item.target} aria-label={`${item.title}解锁进度`}></progress>
              {/if}
              <span class="item-progress">{item.progress.toLocaleString()} / {item.target.toLocaleString()}</span>
            </div>
          </div>
        </article>
      {/each}
    </div>
    {#if visible.length === 0}<p class="empty">没有符合筛选条件的成就，试试其他分类或关键词。</p>{/if}
    <details class="rules">
      <summary>成就统计规则</summary>
      <p>陪伴时长按应用运行时每分钟的经验结算累计，陪伴天数按本地自然日统计。成长进度记录历史最高值；成就解锁后永久保留，重置游戏数据会清空收藏。旧存档会依据现有属性、衣装、星级与可确认的任务记录补发成就。</p>
    </details>
  {/if}
</section>

<style>
  .achievement-page { width: 100%; max-width: 800px; margin: 0 auto; color: #334155; }
  .collection { padding: 16px; background: white; border: 1px solid #e2e8f0; border-radius: 12px; }
  .collection-top { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  h1 { display: flex; align-items: center; gap: 7px; font-size: 18px; font-weight: 700; color: #1e293b; }
  .title-icon { display: flex; color: #5f813b; }
  .collection .complete { display: flex; align-items: center; gap: 6px; color: #4d7c0f; }
  .collection p { margin-top: 5px; font-size: 12px; line-height: 1.5; color: #64748b; }
  .collection-count { white-space: nowrap; text-align: right; }
  .collection-count strong { font-size: 28px; font-weight: 700; color: #4d7c0f; }
  .collection-count span { font-size: 14px; color: #64748b; }
  .progress-label { display: flex; justify-content: space-between; gap: 8px; font-size: 12px; color: #64748b; margin-bottom: 7px; }
  .collection .progress-label { margin-top: 14px; font-size: 11px; }
  progress { display: block; width: 100%; height: 5px; border: 0; border-radius: 10px; overflow: hidden; background: #e2e8f0; }
  progress::-webkit-progress-bar { background: #e2e8f0; border-radius: 10px; }
  progress::-webkit-progress-value { background: #84a653; border-radius: 10px; }
  progress::-moz-progress-bar { background: #84a653; border-radius: 10px; }
  .collection-progress { height: 6px; }
  .categories { display: grid; grid-template-columns: repeat(7, minmax(0, 1fr)); gap: 4px; margin: 14px 0 12px; }
  .categories button { padding: 9px 0; border-radius: 7px; font-size: 12px; color: #64748b; white-space: nowrap; }
  .category-count { margin-left: 4px; font-size: 10px; opacity: .75; }
  .categories button.selected { color: #4d7c0f; background: #edf4e3; font-weight: 600; }
  .tools { display: flex; flex-direction: column; gap: 10px; }
  input, select { border: 1px solid #e2e8f0; border-radius: 8px; background: white; padding: 8px 10px; font-size: 12px; }
  input { width: 100%; min-width: 0; }
  select { width: 100%; padding-right: 30px; }
  .filter-row { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  .status-tabs { display: flex; align-items: center; gap: 3px; background: #eaf0e6; padding: 3px; border-radius: 8px; }
  .status-tabs button { padding: 6px 10px; font-size: 12px; white-space: nowrap; color: #64748b; border-radius: 5px; }
  .status-tabs button.selected { background: white; color: #3f6212; box-shadow: 0 1px 3px #33415515; }
  .sort { width: 115px; flex-shrink: 0; }
  button:focus-visible, input:focus-visible, select:focus-visible, summary:focus-visible { outline: 2px solid #65a30d; outline-offset: 3px; }
  .result-count { margin: 12px 0 8px; color: #64748b; font-size: 11px; }
  .cards { display: flex; flex-direction: column; gap: 8px; }
  .card { display: grid; grid-template-columns: 42px minmax(0, 1fr); gap: 12px; padding: 14px; border: 1px solid #e2e8f0; border-radius: 10px; background: white; }
  .card.unlocked { border-color: #d4dfbf; background: #fcfdf9; }
  .badge { width: 42px; height: 42px; border-radius: 10px; background: #f1f5f9; display: grid; place-items: center; color: #94a3b8; }
  .unlocked .badge { background: #edf4e3; color: #5f813b; }
  .card-content { min-width: 0; }
  .card-heading { display: flex; align-items: flex-start; justify-content: space-between; gap: 8px; }
  h2 { font-size: 14px; line-height: 1.5; font-weight: 600; color: #1e293b; }
  .category-label { display: inline-block; margin-left: 8px; font-size: 10px; font-weight: 400; color: #64748b; vertical-align: middle; }
  .state { display: flex; align-items: center; gap: 3px; flex-shrink: 0; padding-top: 2px; font-size: 10px; color: #94a3b8; }
  .unlocked .state { color: #4d7c0f; }
  .condition { font-size: 12px; line-height: 1.7; color: #64748b; margin: 4px 0 8px; overflow-wrap: anywhere; }
  .card-bottom { display: flex; align-items: center; justify-content: space-between; gap: 12px; min-height: 15px; }
  .card-bottom progress { flex: 1; min-width: 0; }
  .item-progress { font-size: 11px; color: #64748b; white-space: nowrap; font-variant-numeric: tabular-nums; }
  .date { color: #64748b; font-size: 10px; }
  .empty { padding: 40px 16px; text-align: center; color: #64748b; font-size: 13px; }
  .rules { font-size: 11px; line-height: 1.9; color: #64748b; margin: 18px 0 10px; }
  .rules summary { cursor: pointer; }
  .rules p { margin-top: 8px; }
  .error { display: flex; justify-content: space-between; align-items: center; background: #fff1f2; padding: 12px; border-radius: 10px; margin-top: 16px; font-size: 13px; color: #be123c; }
  .error button { text-decoration: underline; }
  @media (max-width: 560px) { .category-count { display: none; } .card { padding: 12px; gap: 10px; } }
  @media (max-width: 380px) { .status-tabs button { padding: 6px 8px; } .sort { width: 103px; } .category-label { display: none; } }
</style>
