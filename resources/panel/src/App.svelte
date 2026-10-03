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

  let local_version = "";
  let latest_version = "";
  let need_update = false;
  function fetchVersionInfo() {
    fetch("/api/version")
      .then((res) => res.json())
      .then((d) => {
        local_version = d["local_version"];
        latest_version = d["latest_version"];
        need_update = d["need_update"];
      });
  }

  updateProfile();

  fetchVersionInfo();
  setTimeout(
    () => {
      fetchVersionInfo();
    },
    10 * 60 * 1000,
  );

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
  onDestroy(() => { unsubscribe(); clearTimeout(noticeTimer); });
</script>

<main>
  <!-- tab buttons -->
  <div class="flex flex-row overflow-x-auto bg-white sticky top-0 z-20 shadow-md">
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
  <div class="flex flex-col p-4 pt-4 bg-gray-50 z-10">
    <div class:hide={activeTab !== 0}>
      <Profile {attributes} {expdiff} {clothes} {buffs} {starcnt} />
    </div>
    <div class:hide={activeTab !== 1}>
      <Task {attributes} {expdiff} {starcnt} />
    </div>
    {#if activeTab === 2}<Achievement />{/if}
    <div class:hide={activeTab !== 3}>
      <Custom current={clothes.current} />
    </div>
    <div class:hide={activeTab !== 4}>
      <Setting bind:account_info={account_info} />
    </div>
    <div class:hide={activeTab !== 5}>
      <Rank {account_info} {attributes} {starcnt} />
    </div>
    <div class:hide={activeTab !== 6}>
      <Document {latest_version} {local_version} />
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
