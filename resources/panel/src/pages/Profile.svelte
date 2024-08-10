<script>
  import { Tooltip, Button, Modal, Alert} from "flowbite-svelte";
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
  import Progress from "../components/Progress.svelte";

  const clothesImages = [imgClothes1, imgClothes2, imgClothes3];
  const attributeArray = ["speed", "endurance", "strength", "will", "intellect"];

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

  $: currentExp = attributes.exp;
  $: buycost = Math.floor(10 * Math.pow(1.5, attributes.buycnt));
  $: revertgain = Math.floor(
    (10 * Math.pow(1.5, Math.max(attributes.buycnt - 1, 0))) / 2,
  );

  // current time to next time point
  let timeToNextPoint = 60 - new Date().getSeconds();
  setInterval(() => {
    timeToNextPoint = 60 - new Date().getSeconds();
  }, 1000);

  // modal
  let addModal = false;
  let revertModal = false;
  let targetAttr = "speed";
  function attrHandle() {
    if (currentExp < buycost) {
      return;
    }
    fetch(`/api/attr/${targetAttr}`, { method: "POST" })
      .then((res) => res.json())
      .then((data) => {
        console.log(data);
      });
  }
  function revertHandle() {
    if (attributes[targetAttr] <= 0) {
      alert("属性值不足");
    }
    fetch(`/api/attr/${targetAttr}`, { method: "DELETE" })
      .then((res) => res.json())
      .then((data) => {
        console.log(data);
      });
  }

  function changeClothes(id) {
    if (!clothes.unlock[id] || id == clothes.current) {
      return;
    }
    clothes.current = id;
    fetch(`/api/clothes/${id}`, { method: "POST" });
  }

  const tooltips = [
    "前期智力对经验值的加成非常可观",
    "一些任务是可重复完成的",
    "就算我没有在运行，任务倒计时也是在进行的",
    "我的头围是 53cm",
    "经验值可用于加点，但加点的消耗会越来越多",
    "装扮完可以试试给我拍张照",
    "设置里可以重置游戏数据",
    "没有毅力的话，任务的成功率最高为 80%",
    "点击发团可以快速打开面板",
    "登录账号后，轴芯等级也可提升经验获取量",
    "获得轴芯等级需要在 B 站账号主页佩戴粉丝牌"
  ];
  let cur_tip = 0;
  setInterval(()=>{
    cur_tip = Math.floor(Math.random() * tooltips.length);
  }, 10000);
</script>

<div>
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
            ><button
              on:click={() => {
                revertModal = true;
                targetAttr = attr;
              }}><img class="icon-button mr-2" src={minusIcon} alt="" /></button
            >
            <span>{attributes[attr]}</span>
            <button
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
      <Button disabled={currentExp < buycost} on:click={attrHandle}>确认</Button
      >
      <Button color="alternative">取消</Button>
    </Modal>
    <Modal title="属性撤销" bind:open={revertModal} size="xs" autoclose>
      <h3 class="mb-5 text-lg font-normal text-gray-500">
        此次操作会返还 {revertgain} EXP，确认撤销吗？
      </h3>
      <Button on:click={revertHandle}>确认</Button>
      <Button color="alternative">取消</Button>
    </Modal>
  </div>
  <div class="flex items-center justify-center mb-4">
    <div class="w-32 h-64 overflow-hidden">
      <img src={clothesImages[clothes.current]} alt="avatar" />
    </div>
    <div style="height: 128px; width: 128px; margin-left: 2rem;">
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
        on:keypress={()=>{}}
        role="button"
        on:click={() => changeClothes(i)}
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
