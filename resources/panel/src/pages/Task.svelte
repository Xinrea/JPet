<script>
  let currentTask = {
    id: 1,
    name: "发呆",
    desc: "简单的发呆中",
    done: false,
    remain: 60,
  };
</script>

<div>
  <div class="task mb-8">
    <div class="header">
      <span>当前任务 - {currentTask.name}</span>
      {#if currentTask.done}
        <span>已完成</span>
      {:else}
        <span>剩余时长：{currentTask.remain}s</span>
      {/if}
    </div>

    <div class="content text-gray-900">{currentTask.desc}</div>
  </div>
  <div class="task">
    <div class="header">任务列表</div>
    <div class="content">
      <ul>
        <li>任务1</li>
        <li>任务2</li>
        <li>任务3</li>
      </ul>
    </div>
  </div>
</div>

<style>
  .task {
    border-radius: 0.25rem;
    border: 1px solid #e2e8f0;
    overflow: hidden;
  }
  .task .header {
    display: flex;
    justify-content: space-between;
    color: white;
    background-color: #79ca2e;
    padding: 4px 12px;
    font-size: 1.2rem;
    font-weight: bold;
  }

  .task .content {
    padding: 8px;
    background-color: white;
  }
</style>
