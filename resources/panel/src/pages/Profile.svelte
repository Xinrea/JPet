<script>
  import { onMount, onDestroy } from "svelte";
  import { Tooltip, Button, Modal, Alert } from "flowbite-svelte";
  import speedIcon from "../assets/at-sp.png";
  import enduranceIcon from "../assets/at-end.png";
  import strengthIcon from "../assets/at-str.png";
  import willIcon from "../assets/at-will.png";
  import intellectIcon from "../assets/at-int.png";
  import addIcon from "../assets/add.svg";
  import minusIcon from "../assets/minus.svg";
  import imgClothes1 from "../assets/c1.png";
  import imgClothes2 from "../assets/c2.png";
  import imgClothes3 from "../assets/c3.png";
  import starIcon from "../assets/star.png";
  import crownIcon from "../assets/crown.png";
  import starOutlineIcon from "../assets/star_outline.png";
  import Progress from "../components/Progress.svelte";
  import BuffIcon from "../components/BuffIcon.svelte";

  const clothesImages = [imgClothes1, imgClothes2, imgClothes3];
  const attributeArray = [
    "speed",
    "endurance",
    "strength",
    "will",
    "intellect",
  ];

  export let clothes = {
    current: 0,
    unlock: [true, false, false],
  };

  let clothesList = [
    {
      id: 0,
      name: "绿色",
    },
    {
      id: 1,
      name: "粉色",
    },
    {
      id: 2,
      name: "冬装",
    },
  ];

  export let attributes = {
    exp: 0,
    speed: 0,
    endurance: 0,
    strength: 0,
    will: 0,
    intellect: 0,
    buycnt: 0,
  };

  export let expdiff = 0;

  export let buffs = ["live", "dynamic", "guard"];

  export let starcnt = 0;
  export let online = false;
  export let expProgress = 0;
  export let buycost = 0;
  export let revertgain = 0;
  export let starAvailable = false;
  let error = "";
  let busy = false;

  $: currentExp = attributes.exp;
  let timeToNextPoint = 60;
  let receivedAt = 0;
  $: { expProgress; receivedAt = performance.now(); }
  function updateTimeToNextPoint() {
    const elapsed = online ? (performance.now() - receivedAt) / 1000 : 0;
    timeToNextPoint = Math.max(0, 60 - expProgress - elapsed);
  }
  onMount(() => {
    const timer = setInterval(updateTimeToNextPoint, 100);
    return () => clearInterval(timer);
  });
  async function gameAction(url, method = "POST") {
    if (!online || busy) return;
    busy = true; error = "";
    try {
      const response = await fetch(url, { method });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "操作失败，请重试");
    } catch (failure) { error = failure.message; }
    finally { busy = false; }
  }

  // modal
  let addModal = false;
  let revertModal = false;
  let targetAttr = "speed";
  function attrHandle() {
    if (currentExp < buycost) {
      return;
    }
    if (attributes[targetAttr] >= 100 + 10 * starcnt) {
      return;
    }
    gameAction(`/api/attr/${targetAttr}`);
  }
  function revertHandle() {
    if (attributes[targetAttr] <= 0) {
      error = "属性值不足"; return;
    }
    gameAction(`/api/attr/${targetAttr}`, "DELETE");
  }

  function changeClothes(id) {
    if (!clothes.unlock[id] || id == clothes.current) {
      return;
    }
    gameAction(`/api/clothes/${id}`);
  }

  let starModal = false;

  function fetchStar() {
    gameAction("/api/star");
    starModal = false;
  }

  const tooltips = [
    "前期智力对经验值的加成非常可观",
    "一些任务是可重复完成的",
    "关闭或断网时，任务与经验进度会暂停",
    "我的头围是 53cm",
    "经验值可用于加点，但加点的消耗会越来越多",
    "装扮完可以试试给我拍张照",
    "设置里可以重置游戏数据",
    "没有毅力的话，任务的成功率最高为 70%",
    "点击发团可以快速打开面板",
    "登录账号后，轴芯等级可以提升经验获取量",
    "轴伊的一些活动可以触发经验加成",
    "鼠标中键点击加点可以跳过确认",
  ];
  let cur_tip = 0;
  const tipTimer = setInterval(() => {
    cur_tip = Math.floor(Math.random() * tooltips.length);
  }, 10000);
  onDestroy(() => clearInterval(tipTimer));
</script>

<div>
  {#if error}<p class="mb-3 text-sm text-red-600" role="alert">{error}</p>{/if}
  <div class="w-full mb-4 rounded overflow-hidden">
    <!-- table of attributes -->
    <table class="w-full">
      <tr class="at-table">
        <th>
          <span><img class="icon" src={speedIcon} alt="" />速度</span><Tooltip
            class="z-30">影响任务完成所需的时间</Tooltip
          ></th
        >
        <th>
          <span>
            <img class="icon" src={enduranceIcon} alt="" />耐力
          </span><Tooltip class="z-30">任务所需的基础属性</Tooltip></th
        >
        <th>
          <span>
            <img class="icon" src={strengthIcon} alt="" />力量
          </span><Tooltip class="z-30">任务所需的基础属性</Tooltip></th
        >
        <th>
          <span>
            <img class="icon" src={willIcon} alt="" />毅力
          </span><Tooltip class="z-30">影响任务完成的成功率</Tooltip></th
        >
        <th>
          <span>
            <img class="icon" src={intellectIcon} alt="" />智力
          </span><Tooltip class="z-30">影响经验获取的效率</Tooltip></th
        >
      </tr>
      <tr>
        {#each attributeArray as attr}
          <td
            ><span class="flex justify-center align-middle"
              ><button disabled={busy || !online}
                on:mousedown={(e) => {
                  if (e.button == 1) {
                    targetAttr = attr;
                    revertHandle();
                  }
                }}
                on:click={() => {
                  revertModal = true;
                  targetAttr = attr;
                }}
                ><img class="icon-button mr-2" src={minusIcon} alt="" /></button
              >
              <span>{attributes[attr]}</span>
              <button disabled={busy || !online}
                on:mousedown={(e) => {
                  if (e.button == 1) {
                    targetAttr = attr;
                    attrHandle();
                  }
                }}
                on:click={() => {
                  addModal = true;
                  targetAttr = attr;
                }}><img class="icon-button ml-2" src={addIcon} alt="" /></button
              ></span
            ></td
          >
        {/each}
      </tr>
    </table>
    <Modal title="属性加点" bind:open={addModal} size="xs" autoclose>
      <h3 class="mb-5 text-lg font-normal text-gray-500">
        此次操作需要消耗 {buycost} EXP，后续撤销仅会返还一半，确认加点吗？
      </h3>
      <Button
          disabled={busy || !online || currentExp < buycost ||
            attributes[targetAttr] >= 100 + 10 * starcnt}
          on:click={attrHandle}>确认</Button
      >
      <Button color="alternative">取消</Button>
    </Modal>
    <Modal title="属性撤销" bind:open={revertModal} size="xs" autoclose>
      <h3 class="mb-5 text-lg font-normal text-gray-500">
        此次操作会返还 {revertgain} EXP，确认撤销吗？
      </h3>
      <Button disabled={busy || !online} on:click={revertHandle}>确认</Button>
      <Button color="alternative">取消</Button>
    </Modal>
  </div>
  <div class="flex relative items-center justify-center mb-4">
    <div class="w-32 h-64 overflow-hidden">
      <img src={clothesImages[clothes.current]} alt="avatar" />
    </div>
    <div style="height: 128px; width: 128px; margin-left: 2rem;">
      <Modal title="星级提升" bind:open={starModal} size="xs" autoclose>
        <h3 class="mb-5 text-lg font-normal text-gray-500">
          此次操作会消耗所有属性各 53 点换取一颗星，要换取吗？
        </h3>
        <Button disabled={!starAvailable || !online || busy} on:click={fetchStar}>确认</Button>
        <Button color="alternative">取消</Button>
      </Modal>
      <a
        href={"#"}
        on:click={() => {
          starModal = true;
        }}
        class="flex justify-center absolute cursor-pointer flex-col"
        style="width: 128px; scale: 0.9; top: 0;"
      >
        {#if starcnt > 0}
          <div class="justify-center flex">
            {#each { length: Math.floor(starcnt / 5) } as _, i}
              <img class="rank-icon" alt="" src={crownIcon} />
            {/each}
          </div>
          <div class="justify-center flex">
            {#each { length: starcnt % 5 } as _, i}
              <img class="rank-icon" alt="" src={starIcon} />
            {/each}
          </div>
        {:else}
          <img class="rank-icon" alt="" src={starOutlineIcon} />
        {/if}
      </a>
      <Tooltip>星星提升经验获取量和属性上限、降低任务成功率，也可消耗星星解锁任务队列容量</Tooltip>
      <Progress max={60} value={60 - timeToNextPoint}>
        <div
          style="position: absolute; left: 50%; top: 50%; transform: translate(-50%,-50%); text-align: center;"
        >
          <span style="border-bottom: 1px solid black; font-size: 1.5rem;"
            >{currentExp}</span
          >
          <div style="font-size: 0.8rem;">EXP</div>
        </div>
      </Progress>
      <Tooltip>下次增加{expdiff}点经验</Tooltip>
      <div id="buff-ref" class="flex items-center justify-center mt-4">
        {#each buffs as buff}
          <BuffIcon type={buff} />
        {/each}
      </div>
    </div>
  </div>
  <div class="flex flex-col justify-center w-full -ml-4 fixed bottom-8">
    <div class="flex justify-center w-full mb-8 px-2">
      <Alert border color="green" dismissable>
        <span class="font-medium">小提示：</span>
        {tooltips[cur_tip]}
      </Alert>
    </div>
    <div class="flex justify-center w-full">
      {#each clothesList as c, i}
        <div
          tabindex={i}
          on:keypress={() => {}}
          role="button"
          aria-disabled={busy || !online} on:click={() => changeClothes(i)}
          class="choice-item"
          class:disabled={!clothes.unlock[i]}
          style="background-image: url({clothesImages[i]});"
        ></div>
        {#if !clothes.unlock[i]}
          <Tooltip>目前还未解锁，请提升属性完成任务解锁</Tooltip>
        {/if}
      {/each}
    </div>
  </div>
</div>

<style>
  .at-table {
    width: 100%;
    background-color: #79ca2e;
    color: #ffffff;
  }

  .at-table th {
    font-size: 0.8rem;
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
    font-size: 0.8rem;
  }

  .icon {
    display: inline;
    width: 1.2rem;
    height: 1.2rem;
    margin-right: 0.5rem;
  }

  .icon-button {
    visibility: hidden;
    width: 1.2rem;
    cursor: pointer;
  }

  .rank-icon {
    width: 28px;
    height: 28px;
    object-fit: contain;
    flex: 0 0 28px;
  }

  td:hover .icon-button {
    visibility: visible;
  }

  .choice-item {
    border-radius: 50%;
    width: 80px;
    height: 80px;
    margin-right: 16px;
    opacity: 0.5;
    cursor: pointer;
    transition: all 0.3s ease-in-out;
    background-size: cover;
  }

  .choice-item.disabled {
    filter: grayscale(100%);
  }

  .choice-item:hover {
    opacity: 1;
    margin-left: 24px;
    margin-right: 24px;
  }

  .choice-item:last-child {
    margin-right: 0;
  }
</style>
