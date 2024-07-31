<script>
  import { Progressbar, Tooltip } from "flowbite-svelte";
  import { sineOut } from "svelte/easing";
  import speedIcon from "../assets/at-sp.png";
  import enduranceIcon from "../assets/at-end.png";
  import strengthIcon from "../assets/at-str.png";
  import willIcon from "../assets/at-will.png";
  import intellectIcon from "../assets/at-int.png";
  import imgCloth1 from "../assets/c1.png";
  import imgCloth2 from "../assets/c2.png";
  import imgCloth3 from "../assets/c3.png";
  import Progress from "../components/Progress.svelte";

  const clothImages = [imgCloth1, imgCloth2, imgCloth3];


  let currentCloth = 0;
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

  export let attributes = {
    exp: 0,
    speed: 0,
    endurance: 0,
    strength: 0,
    will: 0,
    intellect: 0,
  };

  $: currentExp = attributes.exp;
  $: speed = attributes.speed;
  $: endurance = attributes.endurance;
  $: strength = attributes.strength;
  $: will = attributes.will;
  $: intellect = attributes.intellect;

  // current time to next time point
  let timeToNextPoint = 60 - new Date().getSeconds();
  setInterval(() => {
    timeToNextPoint = 60 - new Date().getSeconds();
  }, 1000);
</script>

<div>
  <div class="w-full mb-4 rounded overflow-hidden">
    <!-- table of attributes -->
    <table class="w-full">
      <tr class="at-table">
        <th>
          <span><img class="icon" src={speedIcon} alt="" />速度</span><Tooltip
            >影响任务完成所需的时间</Tooltip
          ></th
        >
        <th>
          <span>
            <img class="icon" src={enduranceIcon} alt="" />耐力
          </span><Tooltip>任务所需的基础属性</Tooltip></th
        >
        <th>
          <span>
            <img class="icon" src={strengthIcon} alt="" />力量
          </span><Tooltip>任务所需的基础属性</Tooltip></th
        >
        <th>
          <span>
            <img class="icon" src={willIcon} alt="" />毅力
          </span><Tooltip>影响任务完成的成功率</Tooltip></th
        >
        <th>
          <span>
            <img class="icon" src={intellectIcon} alt="" />智力
          </span><Tooltip>影响经验获取的效率</Tooltip></th
        >
      </tr>
      <tr>
        <td>{speed}</td>
        <td>{endurance}</td>
        <td>{strength}</td>
        <td>{will}</td>
        <td>{intellect}</td>
      </tr>
    </table>
  </div>
  <div class="flex items-center justify-center mb-4">
    <div class="w-32 h-64 overflow-hidden">
      <img src={clothImages[currentCloth]} alt="avatar" />
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

  .icon {
    display: inline;
    width: 1.5rem;
    height: 1.5rem;
    margin-right: 0.5rem;
  }
</style>
