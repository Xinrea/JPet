<script>
  import endIcon from "../assets/at-end.png";
  import speedIcon from "../assets/at-sp.png";
  import strengthIcon from "../assets/at-str.png";
  import willIcon from "../assets/at-will.png";
  import intellectIcon from "../assets/at-int.png";
  import { Tooltip } from "flowbite-svelte";

  export let attribute = "endurance";
  export let value = 0;
  export let fullfill = false;

  let iconMap = {
    endurance: endIcon,
    speed: speedIcon,
    strength: strengthIcon,
    will: willIcon,
    intellect: intellectIcon,
  };

  let attributeDesc = {
    endurance: "耐力",
    speed: "速度",
    strength: "力量",
    will: "毅力",
    intellect: "智力",
    exp: "经验",
  };
</script>

<span style="margin-right: 0.5rem;">
  <span style="font-size: 12px;" class:lack={!fullfill}>
    {#if attribute === "exp"}
      <span style="font-size: 0.8rem; color: #66ac66">经验</span>
    {:else}
      <img
        class="at-icon"
        src={iconMap[attribute]}
        alt={attributeDesc[attribute]}
      />
    {/if}
    x{value}
  </span>
  <Tooltip>{attributeDesc[attribute]}</Tooltip>
</span>

<style>
  .at-icon {
    display: inline;
    width: 1rem;
    height: 1rem;
    filter: brightness(50%) sepia(100) saturate(100) hue-rotate(130deg)
      opacity(0.6);
  }

  .lack {
    color: #ff666b;
  }
</style>
