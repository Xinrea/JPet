<script>
  import AttributeIcon from "../components/AttributeIcon.svelte";
  import { Button, ButtonGroup, Tooltip, Modal } from "flowbite-svelte";
  import RunIcon from "../assets/run.svg";
  import InfoIcon from "../assets/info.svg";
  import CancelIcon from "../assets/cancel.svg";
  import ClockIcon from "../assets/clock.svg";
  import DoneIcon from "../assets/done.svg";

  export let attributes = {
    exp: 0,
    speed: 0,
    endurance: 0,
    strength: 0,
    will: 0,
    intellect: 0,
  };

  let currentTask = null;

  let timeRemain = 0;

  setInterval(() => {
    if (currentTask && timeRemain > 0) {
      timeRemain = Math.max(
        currentTask.cost -
          Math.floor(Date.now() / 1000 - currentTask.start_time),
        0
      );
    }
    if (currentTask && timeRemain <= 0 && currentTask.status == 1) {
      // update task status
      updateStatus();
    }
  }, 1000);

  let taskList = [
    {
      cost: 60,
      desc: "拧开可乐的瓶盖",
      repeatable: false,
      id: 1,
      requirements: {
        endurance: 10,
        speed: 10,
      },
      rewards: {
        exp: 100,
      },
      start_time: 0,
      end_time: 0,
      success: false,
      status: 0,
      title: "拧瓶盖",
      rate: 0,
    },
    {
      cost: 120,
      desc: "Task 2 description",
      repeatable: false,
      id: 2,
      requirements: {
        strength: 10,
        will: 10,
      },
      rewards: {
        intellect: 30,
      },
      special: {
        title: "新衣装2",
        desc: "解锁新衣装，解锁后可在总览页面切换",
      },
      start_time: 1722510877,
      end_time: 0,
      success: true,
      status: 0,
      title: "Task 2",
      rate: 0,
    },
  ];

  function updateStatus() {
    fetch("/api/task")
      .then((res) => res.json())
      .then((data) => {
        // if undone and started, set currentTask
        // @ts-ignore
        currentTask = data.current;
        taskList = data.list;
        if (currentTask) {
          timeRemain = Math.max(
            currentTask.cost -
              Math.floor(Date.now() / 1000 - currentTask.start_time),
            0
          );
        }
      });
  }

  function startTask(id) {
    fetch(`/api/task/${id}/start`, {
      method: "POST",
    })
      .then((res) => res.json())
      .then((data) => {
        currentTask = data.current;
        taskList = data.list;
      });
  }

  function cancelTask(id) {
    fetch(`/api/task/${id}/cancel`, {
      method: "POST",
    })
      .then((res) => res.json())
      .then((data) => {
        currentTask = data.current;
        taskList = data.list;
      });
  }

  function confirmTask(task) {
    const id = task.id;
    fetch(`/api/task/${id}/confirm`, {
      method: "POST",
    })
      .then((res) => res.json())
      .then((data) => {
        currentTask = data.current;
        taskList = data.list;
        // show rewards
        rewardModal = true;
        rewardTask = task;
      });
  }

  // reward modal
  let rewardModal = false;
  let rewardTask = null;
  
  function calcRate(attrs,task) {
    let lack = 0;
    for (const [key, value] of Object.entries(task.requirements)) {
      if (attrs[key] < value) {
        lack += value - attrs[key];
      }
    }
    if (lack >= 20) {
      return 0;
    }
    lack = lack * 4;
    lack -= attrs.will;
    lack /= 2;
    return Math.min(Math.max(80 - lack, 0), 100);
  }

  function formatDate(time) {
    return new Date(time * 1000).toLocaleString();
  }

  updateStatus();

  // if attributes changed, update rate for each task
  $: {
    taskList.forEach((task) => {
      task.rate = calcRate(attributes, task);
    });
    taskList = [...taskList];
    if (currentTask) {
      currentTask.rate = calcRate(attributes, currentTask);
      currentTask = { ...currentTask };
    }
  }
</script>

<div>
  {#if currentTask}
    <div class="task mb-8">
      <div class="header items-center">
        <span>当前任务 - [T{currentTask.id}] {currentTask.title}</span>
        {#if currentTask.status == 2}
          <span class="flex items-center">
            <span>{currentTask.success ? '任务成功' : '任务失败'}</span>
            <Button class="!p-2 ml-2" color="alternative" size="sm" on:click={()=>confirmTask(currentTask)}><img src={DoneIcon} width="16px" alt="" /></Button>
            <Tooltip class="z-30">确认</Tooltip>
          </span>
        {:else}
          <span class="flex items-center">
            <span>剩余时长：{timeRemain == 0 ? '⌛' : timeRemain}s</span>
            <Button class="!p-2 ml-2" color="alternative" size="sm" on:click={()=>cancelTask(currentTask.id)}><img src={CancelIcon} width="16px" alt="" /></Button>
            <Tooltip class="z-30">中止</Tooltip>
          </span>
        {/if}
      </div>

      <div class="content text-gray-600">
        <p class="mb-4 flex justify-between">
          <span>{currentTask.desc}</span>
          <span class="text-end">
          </span>
        </p>
        <div class="text-gray-500 mb-1 align-middle">
          <span class="badge info">要求</span>
          {#each Object.entries(currentTask.requirements) as [key, value]}
            <AttributeIcon attribute={key} {value} fullfill={attributes[key] >= value} />
          {/each}
        </div>
        <div class="text-gray-500 mb-1 align-middle">
          <span class="badge warn">奖励</span>
          {#each Object.entries(currentTask.rewards) as [key, value]}
            <AttributeIcon attribute={key} {value} fullfill />
          {/each}
        </div>
        {#if currentTask.special}
          <div class="text-gray-500 mb-1 align-middle">
            <span
              class="badge warn
              ">特殊奖励</span
            >
            <span style="font-size: 12px;">{currentTask.special.title}</span>
            <Tooltip>{currentTask.special.desc}</Tooltip>
          </div>
        {/if}
        <div class="text-gray-500 align-middle">
          <span class="badge other">成功率</span>
          <span style="font-size: 12px;">
            {currentTask.rate}%
          </span>
        </div>
      </div>
    </div>
  {/if}
  <div class="task">
    <div class="header">任务列表</div>
    <div class="content">
      <ul>
        {#each taskList as task, i}
          <li class="mb-4" class:archived={task.status == 3}>
            <div class="text-gray-600">
              <p class="mb-1 flex justify-between align-middle items-center">
                <span class="flex items-center"
                  ><span class="badge info">T{task.id}</span><span class="ml-2">
                    <span>{task.title}</span> <span class="icon"><img class="inline" width=12 height=12 src={ClockIcon} alt=""/>{task.cost}s</span>
                  </span></span
                >
                <span class="text-end">
                  {#if task.status != 3}
                    <Button color="alternative" size="sm" disabled={task.rate == 0} on:click={()=>startTask(task.id)}
                      ><img src={RunIcon} width="16px" alt="" /></Button
                    >
                    <Tooltip class="z-30">执行</Tooltip>
                  {:else}
                    <span class="text-sm text-gray-500">{formatDate(task.start_time + task.cost)}</span>
                  {/if}
                </span>
              </p>
              <div class="text-gray-500 mb-1 align-middle">
                <span class="badge info">要求</span>
                {#each Object.entries(task.requirements) as [key, value]}
                  <AttributeIcon attribute={key} {value} fullfill={attributes[key] >= value} />
                {/each}
              </div>
              <div class="text-gray-500 mb-1 align-middle">
                <span
                  class="badge warn
                  ">奖励</span
                >
                {#each Object.entries(task.rewards) as [key, value]}
                  <AttributeIcon attribute={key} {value} fullfill />
                {/each}
              </div>
              {#if task.special}
                <div class="text-gray-500 mb-1 align-middle">
                  <span
                    class="badge warn
                    ">特殊奖励</span
                  >
                  <span style="font-size: 12px;">{task.special.title}</span>
                  <Tooltip>{task.special.desc}</Tooltip>
                </div>
              {/if}
              {#if task.status != 3}
                <div class="text-gray-500 align-middle">
                  <span class="badge other">成功率</span>
                  <span style="font-size: 12px;">
                    {task.rate}%
                  </span>
                </div>
              {/if}
            </div>
            {#if i < taskList.length - 1}
              <hr class="mt-4" />
            {/if}
          </li>
        {/each}
      </ul>
    </div>
    <Modal title="任务奖励" bind:open={rewardModal} size="xs" autoclose outsideclose>
      <h3 class="mb-2 font-normal text-gray-500">获得了如下奖励：</h3>
      <div>
        {#each Object.entries(rewardTask.rewards) as [key, value]}
          <AttributeIcon attribute={key} {value} fullfill />
        {/each}
      </div>
      {#if rewardTask.special}
        <div>
          <span>{rewardTask.special.title}</span>
          <Tooltip>{rewardTask.special.desc}</Tooltip>
        </div>
      {/if}
    </Modal>
  </div>
</div>

<style>
  .task {
    border-radius: 0.25rem;
    border: 1px solid #e2e8f0;
    background-color: white;
    box-shadow: 0 1px 2px 0 rgba(0, 0, 0, 0.05);
    overflow: hidden;
  }
  .task .header {
    display: flex;
    justify-content: space-between;
    color: white;
    background-color: #79ca2e;
    padding: 4px 12px;
    font-size: 1rem;
    font-weight: bold;
  }

  .task .content {
    padding: 8px;
  }

  .badge {
    padding: 4px 8px;
    width: 36px;
    border-radius: 4px;
    color: #fff;
    text-align: center;
    margin: 4px 0px;
    font-size: 12px;
  }

  .badge.info {
    background-color: #79ca2e;
  }

  .badge.warn {
    background-color: #ff666b;
  }

  .badge.other {
    background-color: #6089f6;
  }

  .icon {
    vertical-align: super;
    font-size: 12px;
  }

  .archived {
    opacity: 0.5;
  }
</style>
