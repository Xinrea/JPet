<script>
  import { Progressbar } from "flowbite-svelte";
  import { sineOut } from "svelte/easing";

  const es = new EventSource("/api/sse");
  es.onmessage = (event) => {
    console.log(event);
  };

  let currentExp = 0;
  let currentCloth = 1;
  let clothList = [
    {
      id: 1,
      name: "绿色",
      unlock: true,
    },
    {
      id: 2,
      name: "粉色",
      unlock: false,
    },
    {
      id: 3,
      name: "冬装",
      unlock: false,
    },
  ];
  let attributes = {
    hp: 100,
    mp: 100,
    str: 1,
    dex: 1,
    vit: 1,
    int: 8,
    mind: 5,
  };
  let currentTask = {
    id: 1,
    name: "任务1",
  };

  updateProfile();

  // fetch current status
  function updateProfile() {
    fetch("/api/profile")
      .then((res) => res.json())
      .then((data) => {
        currentExp = data.exp;
        currentCloth = data.cloth.active;
        clothList = data.cloth.list;
        attributes = data.attributes;
        console.log(data);
      });
  }

  // calculate exp to next level
  const ExpToLevel = (/** @type {number} */ exp) => {
    for (let i = 0; i < 54; i++) {
      if (exp < levelToExp(i)) {
        return {
          level: i,
          exp: levelToExp(i) - exp,
          nextLevelExp: levelToExp(i),
        };
      }
      exp -= levelToExp(i);
    }
  };

  $: levelInfo = ExpToLevel(currentExp);

  const levelToExp = (/** @type {number} */ level) => {
    return Math.ceil(level * level + 30);
  };

  // current time to next time point
  let timeToNextPoint = 60 - new Date().getSeconds();
  setInterval(() => {
    timeToNextPoint = 60 - new Date().getSeconds();
    if (timeToNextPoint === 60) {
      updateProfile();
    }
  }, 1000);
</script>

<div>
  <div class="w-full mb-4 rounded overflow-hidden">
    <!-- table of attributes -->
    <table class="w-full">
      <tr class="at-table">
        <th>血量</th>
        <th>魔力</th>
        <th>力量</th>
        <th>敏捷</th>
        <th>体质</th>
        <th>智力</th>
        <th>精神</th>
      </tr>
      <tr>
        <td>{attributes.hp}</td>
        <td>{attributes.mp}</td>
        <td>{attributes.str}</td>
        <td>{attributes.dex}</td>
        <td>{attributes.vit}</td>
        <td>{attributes.int}</td>
        <td>{attributes.mind}</td>
      </tr>
    </table>
  </div>
  <div class="flex items-center justify-center mb-4">
    <div class="w-32 h-64 overflow-hidden">
      <img src="src/assets/c{currentCloth}.png" alt="avatar" />
    </div>
  </div>
</div>

<p class="text-sm text-gray-500 dark:text-gray-400 mb-2">
  当前等级：{ExpToLevel(currentExp).level}
</p>
<Progressbar
  animate
  tweenDuration={1000}
  easing={sineOut}
  progress={(100 * (levelInfo.nextLevelExp - levelInfo.exp)) /
    levelInfo.nextLevelExp}
  class="mb-2"
/>
<p class="text-sm text-gray-500 dark:text-gray-400">
  距离下个等级：{ExpToLevel(currentExp).exp}
</p>
<p class="text-sm text-gray-500 dark:text-gray-400">
  总经验：{currentExp}
</p>
<p class="text-sm text-gray-500 dark:text-gray-400">
  距离下次获取经验：{timeToNextPoint} 秒
</p>

<style>
  .at-table {
    width: 100%;
    background-color: #79ca2e;
    color: #ffffff;
  }

  .at-table th {
    padding: 0.5rem;
  }

  tr {
    border-radius: 0.25rem;
    border: 1px solid #e2e8f0;
  }

  td {
    padding: 0.2rem;
    text-align: center;
    background-color: rgb(255, 255, 255);
  }
</style>
