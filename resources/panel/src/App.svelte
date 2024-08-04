<script>
  import "./app.css";
  import Profile from "./pages/Profile.svelte";
  import Task from "./pages/Task.svelte";
  import Document from "./pages/Document.svelte";
  import Setting from "./pages/Setting.svelte";

  let activeTab = 0;
  let tabs = [
    { name: "总览" },
    { name: "任务" },
    { name: "自定义" },
    { name: "设置" },
    { name: "说明" },
  ];

  let attributes = {
    exp: 100,
    speed: 0,
    endurance: 0,
    strength: 0,
    will: 0,
    intellect: 0,
  };
  let expdiff = 0;
  let clothes = {
    current: 0,
    unlock: [true, false, false],
  };

  // fetch current status
  function updateProfile() {
    fetch("/api/profile")
      .then((res) => res.json())
      .then((data) => {
        clothes = data.clothes;
        attributes = data.attributes;
        expdiff = data.expdiff;
        console.log(data);
      });
  }

  const es = new EventSource("/api/sse");
  es.onmessage = (event) => {
    console.log(event);
  };

  updateProfile();

  setInterval(() => {
    updateProfile();
  }, 1000);
</script>

<main>
  <!-- tab buttons -->
  <div class="flex flex-row bg-white sticky top-0 z-20 shadow-md">
    {#each tabs as tab, index}
      <button
        class="inline-block text-sm font-medium text-center disabled:cursor-not-allowed p-4 border-primary-600 dark:text-primary-500 dark:border-primary-500"
        class:active={activeTab === index}
        on:click={() => (activeTab = index)}>{tab.name}</button
      >
    {/each}
  </div>
  <div class="flex flex-col p-2 pt-4 bg-gray-50 z-10">
    <div class:hide={activeTab !== 0}>
      <Profile {attributes} {expdiff} {clothes} />
    </div>
    <div class:hide={activeTab !== 1}>
      <Task {attributes} />
    </div>
    <div class:hide={activeTab !== 2}>
      <p class="text-sm text-gray-500 dark:text-gray-400">
        <b>Dashboard:</b>
        Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor
        incididunt ut labore et dolore magna aliqua.
      </p>
    </div>
    <div class:hide={activeTab !== 3}>
      <Setting />
    </div>
    <div class:hide={activeTab !== 4}>
      <Document />
    </div>
  </div>
</main>

<style>
  .hide {
    display: none;
  }

  .active {
    @apply text-primary-600 border-b-2;
  }
</style>
