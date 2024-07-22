<script>
  import { Progressbar } from "flowbite-svelte";
  import { sineOut } from "svelte/easing";

  const es = new EventSource("/api/sse");
  es.onmessage = (event) => {
    console.log(event);
  };

  let currentExp = 0;

  updateProfile();
  
  // fetch current status
  function updateProfile() {
    fetch("/api/profile")
      .then((res) => res.json())
      .then((data) => {
          currentExp = data.exp;
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
