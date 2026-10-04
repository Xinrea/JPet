<script>
  import "./app.css";
  import Profile from "./pages/Profile.svelte";
  import Task from "./pages/Task.svelte";
  import Document from "./pages/Document.svelte";
  import Setting from "./pages/Setting.svelte";
  import Custom from "./pages/Custom.svelte";
  import Rank from "./pages/Rank.svelte";
  import Achievement from "./pages/Achievement.svelte";
  import AchievementIcon from "./components/AchievementIcon.svelte";
  import { onDestroy } from "svelte";
  import { Indicator } from "flowbite-svelte";
  import { sse } from "./sse.js";
  import { reportFrontend, reportFrontendError } from "./logger.js";

  let activeTab = 0;
  let tabs = [
    { name: "总览" },
    { name: "任务" },
    { name: "成就" },
    { name: "装扮" },
    { name: "设置" },
    { name: "排行榜"},
    { name: "说明" },
  ];

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
      activeTab = 6;
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

<main>
  <!-- tab buttons -->
  <div class="panel-tabs panel-scroll flex flex-row overflow-x-auto bg-white z-20 shadow-md">
    {#each tabs as tab, index}
      <button
        class="inline-block shrink-0 relative text-sm font-medium text-center disabled:cursor-not-allowed p-4 border-primary-600 dark:text-primary-500 dark:border-primary-500"
        class:active={activeTab === index}
        on:click={() => (activeTab = index)}
        >{tab.name}
        {#if index == 6 && need_update}
          <Indicator color="red" border size="md" placement="center-right">
          </Indicator>
        {/if}
      </button>
    {/each}
  </div>
  <!-- 内容滚动区可聚焦，以支持方向键和 Page Up / Page Down。 -->
  <!-- svelte-ignore a11y-no-noninteractive-tabindex -->
  <div class="panel-content panel-scroll flex flex-col p-4 pt-4 bg-gray-50" role="region" aria-label={tabs[activeTab].name + "内容"} tabindex="0">
    <p class="mb-3 rounded border p-3 text-sm" class:border-green-200={game.online} class:bg-green-50={game.online}
      class:border-amber-200={!game.online} class:bg-amber-50={!game.online} role="status">
      {game.online ? "云端已连接 · 任务和经验正在推进" : `${game.cloud?.error || "连接中断"} · 任务和经验已暂停`}
    </p>
    <div class:hide={activeTab !== 0}>
      <Profile {attributes} {expdiff} {clothes} {buffs} {starcnt} online={game.online}
        expProgress={game.exp_progress_seconds ?? 0} buycost={game.buycost ?? 0}
        revertgain={game.revertgain ?? 0} starAvailable={game.star_available ?? false} />
    </div>
    <div class:hide={activeTab !== 1}>
      <Task {attributes} online={game.online} />
    </div>
    {#if activeTab === 2}<Achievement />{/if}
    <div class:hide={activeTab !== 3}>
      <Custom current={clothes.current} />
    </div>
    <div class:hide={activeTab !== 4}>
      <Setting bind:account_info={account_info} />
    </div>
    {#if activeTab === 5}<Rank {account_info} />{/if}
    <div class:hide={activeTab !== 6}>
      <Document bind:updater on:refresh={fetchVersionInfo} />
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
  main {
    display: flex;
    flex-direction: column;
    height: 100%;
    min-width: 0;
    overflow: hidden;
  }

  .panel-tabs {
    flex-shrink: 0;
  }

  .panel-content {
    position: relative;
    flex: 1;
    min-height: 0;
    overflow: auto;
    scrollbar-gutter: stable;
  }

  .panel-content:focus-visible {
    outline: 2px solid theme("colors.primary.500");
    outline-offset: -2px;
  }

  .hide {
    display: none;
  }

  .active {
    @apply text-primary-600 border-b-2;
  }
  .achievement-notice { position: fixed; bottom: 24px; right: 24px; z-index: 50; display: flex; max-width: calc(100vw - 48px); border: 1px solid #d4dfbf; border-radius: 14px; background: #fcfdf9; box-shadow: 0 10px 35px #33415526; color: #4d7c0f; }
  .notice-content { display: flex; flex-direction: column; gap: 5px; padding: 16px 20px; text-align: left; font-size: 13px; }
  .notice-content small { font-size: 11px; }
  .notice-content strong, .notice-content span { display: flex; align-items: center; gap: 6px; }
  .notice-dismiss { align-self: flex-start; padding: 12px; }
</style>
