<script>
  import "./app.css";
  import Profile from "./pages/Profile.svelte";
  import Task from "./pages/Task.svelte";
  import Document from "./pages/Document.svelte";
  import Setting from "./pages/Setting.svelte";
  import Custom from "./pages/Custom.svelte";
  import Rank from "./pages/Rank.svelte";
  import Achievement from "./pages/Achievement.svelte";
  import Conversation from "./pages/Conversation.svelte";
  import AchievementIcon from "./components/AchievementIcon.svelte";
  import { onDestroy } from "svelte";
  import UiIcon from "./components/UiIcon.svelte";
  import fanAvatar from "./assets/fan.png";
  import { sse } from "./sse.js";
  import { reportFrontend, reportFrontendError } from "./logger.js";

  let activeTab = 0;
  let contentViewport;
  const tabs = [
    { name: "总览", icon: "home", title: "今天也一起成长吧！", description: "每一点进步，都值得被记录。", label: "COMPANION" },
    { name: "任务", icon: "task", title: "向下一个目标出发", description: "安排任务，收获属于你们的成长。", label: "TRAINING" },
    { name: "成就", icon: "trophy", title: "闪闪发光的陪伴回忆", description: "收藏一路走来的每个小小里程碑。", label: "COLLECTION" },
    { name: "装扮", icon: "dress", title: "换上今天的好心情", description: "搭配喜欢的装扮，留下可爱的瞬间。", label: "DRESS UP" },
    { name: "设置", icon: "settings", title: "你的专属陪伴设置", description: "声音、互动与通知，都按你的喜好来。", label: "OPTIONS" },
    { name: "对话记录", icon: "chat", title: "每一次聊天，都有迹可循", description: "回看你和轴伊的 AI 对话，找回聊过的点滴。", label: "CONVERSATIONS" },
    { name: "排行榜", icon: "rank", title: "一起见证成长", description: "查看大家的成长记录与自己的排名。", label: "RANKING" },
    { name: "说明", icon: "book", title: "陪伴指南", description: "了解成长规则，发现更多陪伴方式。", label: "GUIDE" },
  ];
  function selectTab(index) {
    activeTab = index;
    if (contentViewport) contentViewport.scrollTop = 0;
  }

  let attributes = {
    exp: 100,
    speed: 0,
    endurance: 0,
    strength: 0,
    will: 0,
    intellect: 0,
    buycnt: 0,
  };
  let starcnt = 0;
  let expdiff = 0;
  let clothes = {
    current: 0,
    unlock: [true, false, false],
  };
  let buffs = [];
  let account_info = null;
  let game = { online: false, cloud: { error: "正在同步云端状态" } };

  // fetch current status
  function updateProfile() {
    fetch("/api/profile")
      .then((res) => {
        if (!res.ok) {
          throw new Error(`profile request failed: HTTP ${res.status}`);
        }
        return res.json();
      })
      .then((data) => {
        game = data;
        clothes = data.clothes;
        attributes = data.attributes;
        expdiff = data.expdiff;
        buffs = data.buffs;
        starcnt = data.starcnt;
        reportFrontend("info", "[App] profile loaded", {
          exp: attributes.exp,
          expdiff,
          starcnt,
          buffCount: buffs.length,
        });
      })
      .catch((error) => {
        reportFrontendError("[App] profile request failed", error);
      });
  }

  let updater = {};
  let need_update = false;
  $: need_update = updater.need_update === true;
  let versionTimer;
  let destroyed = false;
  function fetchVersionInfo() {
    clearTimeout(versionTimer);
    return fetch("/api/version")
      .then((res) => res.json())
      .then((d) => {
        updater = d;
      })
      .catch((error) => reportFrontendError("[App] version status failed", error))
      .finally(() => {
        if (!destroyed) versionTimer = setTimeout(fetchVersionInfo,
          ["checking", "downloading", "verifying", "installing"].includes(updater.state) ? 1000 : 30000);
      });
  }

  updateProfile();

  fetchVersionInfo();

  let achievementNotice = [];
  let noticeTimer;
  const unsubscribe = sse.subscribe((e) => {
    if (!e) {
      return;
    }
    console.log("SSE:", e);
    if (e.data == "TASK_COMPLETE") {
      activeTab = 1;
      console.log("task complete");
    }
    if (e.data == "UPDATE") {
      updateProfile();
    }
    if (e.data === "SOFTWARE_UPDATE") {
      activeTab = 7;
      fetchVersionInfo();
    }
    if (e.data.startsWith("{")) {
      try {
        const message = JSON.parse(e.data);
        if (message.type === "ACHIEVEMENT_UNLOCKED") {
          achievementNotice = message.achievements;
          clearTimeout(noticeTimer);
          noticeTimer = setTimeout(() => achievementNotice = [], 7000);
        }
      } catch (error) { reportFrontendError("[App] invalid achievement event", error); }
    }
  });
  onDestroy(() => { destroyed = true; unsubscribe(); clearTimeout(noticeTimer); clearTimeout(versionTimer); });
</script>

<main class="jpet-panel">
  <header class="panel-header">
    <div class="panel-brand"><img src={fanAvatar} alt="" /><div><span class="brand-eyebrow">DESKTOP COMPANION</span><strong>JPet <span>陪伴手册</span></strong></div></div>
    <div class="header-resources" aria-label="成长数据"><span class="resource-chip"><span class="exp-symbol">EXP</span><strong>{attributes.exp.toLocaleString()}</strong></span><span class="resource-chip star-chip"><UiIcon name="star" size={16} /><strong>{starcnt}</strong></span></div>
  </header>
  <nav class="panel-tabs panel-scroll" aria-label="功能页面">
    {#each tabs as tab, index}
      <button class:active={activeTab === index} aria-current={activeTab === index ? "page" : undefined} on:click={() => selectTab(index)}>
        <UiIcon name={tab.icon} size={20} /><span>{tab.name}</span>
        {#if index === 7 && need_update}<span class="update-dot" aria-label="有新版本"></span>{/if}
      </button>
    {/each}
  </nav>
  <div class="cloud-strip" class:offline={!game.online} role="status"><span class="status-dot"></span><span>{game.online ? "云端已连接 · 任务和经验正在推进" : `${game.cloud?.error || "连接中断"} · 任务和经验已暂停`}</span><UiIcon name="cloud" size={16} /></div>
  <!-- svelte-ignore a11y-no-noninteractive-tabindex -->
  <div bind:this={contentViewport} class="panel-content panel-scroll" role="region" aria-label={tabs[activeTab].name + "内容"} tabindex="0">
    <div class="panel-inner">
      <div class="page-heading"><div><span class="page-eyebrow">{tabs[activeTab].label}</span><h1>{tabs[activeTab].title}</h1><p>{tabs[activeTab].description}</p></div><span class="page-heading-icon" aria-hidden="true"><UiIcon name={tabs[activeTab].icon} size={32} /></span></div>
      <div class="page-view profile-view" class:hide={activeTab !== 0}>
        <Profile {attributes} {expdiff} {clothes} {buffs} {starcnt} online={game.online} expProgress={game.exp_progress_seconds ?? 0} buycost={game.buycost ?? 0} revertgain={game.revertgain ?? 0} starAvailable={game.star_available ?? false} />
      </div>
      <div class="page-view" class:hide={activeTab !== 1}><Task {attributes} online={game.online} /></div>
      {#if activeTab === 2}<div class="page-view"><Achievement /></div>{/if}
      <div class="page-view custom-view" class:hide={activeTab !== 3}><Custom current={clothes.current} /></div>
      <div class="page-view" class:hide={activeTab !== 4}><Setting bind:account_info={account_info} online={game.online} /></div>
      {#if activeTab === 5}<div class="page-view"><Conversation /></div>{/if}
      {#if activeTab === 6}<div class="page-view game-card rank-view"><Rank {account_info} /></div>{/if}
      <div class="page-view document-view" class:hide={activeTab !== 7}><Document bind:updater on:refresh={fetchVersionInfo} /></div>
      <footer class="panel-footer"><span class="footer-line"></span><UiIcon name="spark" size={12} /><span>和轴伊一起，把平凡的日子过得闪闪发光</span><span class="footer-line"></span></footer>
    </div>
  </div>
  {#if achievementNotice.length > 0}
    <div class="achievement-notice" role="status" aria-live="polite">
      <button class="notice-content" on:click={() => { activeTab = 2; achievementNotice = []; }}>
        <strong><AchievementIcon name="trophy" size={18} /> 解锁新成就</strong>
        <span><AchievementIcon name={achievementNotice[0].icon} size={16} /> {achievementNotice[0].title}{achievementNotice.length > 1 ? ` 等 ${achievementNotice.length} 项` : ""}</span>
        <small>点击查看成就收藏</small>
      </button>
      <button class="notice-dismiss" aria-label="关闭成就提示" on:click={() => achievementNotice = []}><AchievementIcon name="close" size={18} /></button>
    </div>
  {/if}
</main>

<style>
  main { display: flex; flex-direction: column; height: 100%; min-width: 0; overflow: hidden; }
  .panel-header { position: relative; display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 18px 26px; flex-shrink: 0; color: white; background: radial-gradient(circle at 75% -40%, #c1f15b77 0 32%, transparent 32.3%), repeating-linear-gradient(125deg, transparent 0 12px, #ffffff08 12px 14px), linear-gradient(110deg, #79c91b, #66b91b); border-bottom: 3px solid #ffffff9c; }
  .panel-header::before { content: ""; position: absolute; inset: 0; pointer-events: none; background-image: radial-gradient(#ffffff24 1.5px, transparent 1.5px); background-size: 10px 10px; mask-image: linear-gradient(90deg, black, transparent 32%); }
  .panel-brand { display: flex; align-items: center; gap: 12px; z-index: 1; }
  .panel-brand img { height: 46px; width: 46px; border-radius: 14px; border: 3px solid white; object-fit: cover; box-shadow: 0 3px 0 #458a1966; }
  .panel-brand strong { display: block; font-size: 24px; font-weight: 900; line-height: 1.2; letter-spacing: -.6px; text-shadow: 0 2px 0 #438b1955; }
  .panel-brand strong span { font-size: 17px; font-weight: 800; margin-left: 4px; letter-spacing: 1px; }
  .brand-eyebrow { font-size: 9px; font-weight: 800; letter-spacing: 2px; opacity: .9; }
  .header-resources { display: flex; align-items: center; gap: 8px; z-index: 1; }
  .resource-chip { display: flex; align-items: center; gap: 8px; min-height: 30px; padding: 3px 11px 3px 5px; color: var(--ink); background: #fff; border-radius: 20px; border: 1px solid #fff; box-shadow: 0 2px 0 #438b1944; font-size: 12px; font-variant-numeric: tabular-nums; }
  .exp-symbol { padding: 4px 7px; color: #fff; background: #f3a943; border-radius: 14px; font-size: 9px; font-weight: 900; font-style: italic; }
  .star-chip { padding-left: 9px; color: #b47a21; }
  .panel-tabs { display: flex; flex-shrink: 0; gap: 4px; padding: 10px 22px; overflow-x: auto; background: #fff; border-bottom: 1px solid var(--line); z-index: 2; }
  .panel-tabs button { position: relative; display: flex; flex: 1 0 auto; align-items: center; justify-content: center; gap: 7px; min-height: 42px; padding: 9px 12px; border: 1px solid transparent; border-radius: 9px; color: #83746b; font-size: 13px; font-weight: 800; white-space: nowrap; transition: background .15s, color .15s; }
  .panel-tabs button:not(.active):hover { color: var(--green-ink); background: var(--green-soft); }
  .panel-tabs button.active { color: #fff; border-color: #65b417; background: var(--green-gradient); box-shadow: inset 0 1px 0 #ffffff66, 0 3px 0 #519e19; text-shadow: 0 1px 1px #458c19; }
  .update-dot { width: 7px; height: 7px; background: #ed7291; border: 1px solid white; border-radius: 50%; position: absolute; right: 6px; top: 6px; }
  .cloud-strip { display: flex; align-items: center; gap: 7px; flex-shrink: 0; padding: 8px 27px; font-size: 11px; color: #63844c; background: #f5faed; border-bottom: 1px solid #e1ead6; }
  .cloud-strip > :global(svg) { margin-left: auto; flex-shrink: 0; }
  .cloud-strip.offline { color: #ad7e37; background: #fff8e9; border-bottom-color: #efe4cd; }
  .cloud-strip.offline :global(.status-dot) { background: #e5af55; box-shadow: 0 0 0 3px #e5af551b; }
  .panel-content { position: relative; flex: 1; min-height: 0; overflow: auto; scrollbar-gutter: stable; padding: 24px 26px 16px; }
  .panel-content:focus-visible { outline: 2px solid var(--green); outline-offset: -2px; }
  .panel-inner { width: 100%; max-width: 980px; margin: 0 auto; }
  .page-heading { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-bottom: 22px; }
  .page-eyebrow { font-size: 10px; font-weight: 900; letter-spacing: 2px; color: var(--green-ink); }
  .page-heading h1 { margin: 4px 0 6px; font-size: 23px; line-height: 1.35; font-weight: 900; color: var(--ink); }
  .page-heading p { font-size: 12px; color: var(--muted); }
  .page-heading-icon { display: grid; place-items: center; width: 58px; height: 58px; flex-shrink: 0; border: 2px solid #fff; border-radius: 18px; color: #79b84c; background: #e7f2d7; box-shadow: 0 3px 0 #dbe8cf; transform: rotate(-6deg); }
  .page-view { min-width: 0; }
  .rank-view { padding: 20px; }
  .panel-footer { display: flex; align-items: center; justify-content: center; gap: 8px; margin: 26px 0 3px; color: #a4ad96; font-size: 10px; }
  .footer-line { height: 1px; max-width: 36px; flex: 1; background: #dce5d0; }
  .hide { display: none; }
  .achievement-notice { position: fixed; bottom: 24px; right: 24px; z-index: 50; display: flex; max-width: calc(100vw - 48px); border: 2px solid #d4e8bc; border-radius: 14px; background: #fff; box-shadow: 0 5px 0 #e1eacd, 0 10px 35px #55643b26; color: var(--green-ink); }
  .notice-content { display: flex; flex-direction: column; gap: 5px; padding: 16px 20px; text-align: left; font-size: 13px; }
  .notice-content small { font-size: 11px; }
  .notice-content strong, .notice-content span { display: flex; align-items: center; gap: 6px; }
  .notice-dismiss { align-self: flex-start; padding: 12px; }
  @media (max-width: 560px) { .panel-header { padding: 14px 16px; } .panel-tabs { padding: 9px 12px; gap: 3px; } .panel-tabs button { gap: 5px; padding: 8px 10px; font-size: 12px; } .panel-tabs button :global(svg) { width: 17px; } .cloud-strip { padding: 8px 17px; } .panel-content { padding: 20px 14px 14px; } .page-heading h1 { font-size: 21px; } .page-heading-icon { width: 48px; height: 48px; } .panel-brand { gap: 8px; } .panel-brand strong span { font-size: 14px; } .brand-eyebrow { font-size: 8px; letter-spacing: 1px; } .header-resources { gap: 5px; } .resource-chip { padding-right: 8px; gap: 4px; font-size: 11px; } }
  @media (max-width: 380px) { .panel-brand strong span { display: none; } .panel-brand img { height: 40px; width: 40px; } .page-heading-icon { display: none; } .panel-footer { font-size: 9px; gap: 5px; } }
</style>
