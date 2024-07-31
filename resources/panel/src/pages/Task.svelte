<script>
  import AttributeIcon from "../components/AttributeIcon.svelte";
  import { Button, ButtonGroup, Tooltip } from "flowbite-svelte";
  import RunIcon from "../assets/run.svg";
  import InfoIcon from "../assets/info.svg";
  import CancelIcon from "../assets/cancel.svg";
  import ClockIcon from "../assets/clock.svg";

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
    if (currentTask && !currentTask.done && timeRemain > 0) {
      timeRemain = Math.max(
        currentTask.cost -
          Math.floor(Date.now() / 1000 - currentTask.start_time),
        0
      );
    }
    if (timeRemain <= 0 && currentTask) {
      // update task status
      updateStatus();
    }
  }, 1000);

  let taskList = [
    {
      cost: 60,
      desc: "拧开可乐的瓶盖",
      done: false,
      id: 1,
      requirements: {
        endurance: 10,
        speed: 10,
      },
      rewards: {
        exp: 100,
      },
      start_time: 0,
      success: false,
      title: "拧瓶盖",
      rate: 0,
    },
    {
      cost: 120,
      desc: "Task 2 description",
      done: false,
      id: 2,
      requirements: {
        strength: 10,
        will: 10,
      },
      rewards: {
        intellect: 30,
      },
      start_time: 0,
      success: false,
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
    return Math.max(80 - lack, 0);
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
      <div class="header">
        <span>当前任务 - [T{currentTask.id}] {currentTask.title}</span>
        <span>剩余时长：{timeRemain}s</span>
      </div>

      <div class="content text-gray-600">
        <p class="mb-4 flex justify-between">
          <span>{currentTask.desc}</span>
          <span class="text-end">
            <ButtonGroup size="sm" class="space-x-px shadow-none">
              <Button on:click={()=>cancelTask(currentTask.id)}><img src={CancelIcon} width="16px" alt="" /></Button>
              <Tooltip>中止</Tooltip>
              <Button><img src={InfoIcon} width="16px" alt="" /></Button>
              <Tooltip>详情</Tooltip>
            </ButtonGroup>
          </span>
        </p>
        <span class="text-gray-500 mb-1 align-middle">
          <span class="badge info">要求</span>
          {#each Object.entries(currentTask.requirements) as [key, value]}
            <AttributeIcon attribute={key} {value} />
          {/each}
        </span>
        <span class="text-gray-500 mb-1 align-middle">
          <span class="badge warn">奖励</span>
          {#each Object.entries(currentTask.rewards) as [key, value]}
            <AttributeIcon attribute={key} {value} fullfill />
          {/each}
        </span>
        <span class="text-gray-500 align-middle">
          <span class="badge other">成功率</span>
          <span style="font-size: 12px;">
            {currentTask.rate}%
          </span>
        </span>
      </div>
    </div>
  {/if}
  <div class="task">
    <div class="header">任务列表</div>
    <div class="content">
      <ul>
        {#each taskList as task}
          <li class="mb-4">
            <div class="text-gray-600">
              <p class="mb-1 flex justify-between align-middle">
                <span
                  ><span class="badge info">T{task.id}</span><span class="ml-2">
                    <span>{task.title}</span> <span class="icon"><img class="inline" width=12 height=12 src={ClockIcon} alt=""/>{task.cost}s</span>
                  </span></span
                >
                <span class="text-end">
                  <ButtonGroup size="sm" class="space-x-px shadow-none">
                    {#if !task.success}
                    <Button disabled={task.rate == 0} on:click={()=>startTask(task.id)}
                      ><img src={RunIcon} width="16px" alt="" /></Button
                    >
                    <Tooltip>执行</Tooltip>
                    {/if}
                    <Button><img src={InfoIcon} width="16px" alt="" /></Button>
                    <Tooltip>详情</Tooltip>
                  </ButtonGroup>
                </span>
              </p>
              <span class="text-gray-500 mb-1 align-middle">
                <span class="badge info">要求</span>
                {#each Object.entries(task.requirements) as [key, value]}
                  <AttributeIcon attribute={key} {value} fullfill={attributes[key] >= value} />
                {/each}
              </span>
              <span class="text-gray-500 mb-1 align-middle">
                <span
                  class="badge warn
                  ">奖励</span
                >
                {#each Object.entries(task.rewards) as [key, value]}
                  <AttributeIcon attribute={key} {value} fullfill />
                {/each}
              </span>
              <span class="text-gray-500 align-middle">
                <span class="badge other">成功率</span>
                <span style="font-size: 12px;">
                  {task.rate}%
                </span>
              </span>
            </div>
            <hr class="mt-4" />
          </li>
        {/each}
      </ul>
    </div>
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
    height: 22px;
    line-height: 22px;
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
</style>
