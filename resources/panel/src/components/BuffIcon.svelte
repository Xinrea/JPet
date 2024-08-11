<script>
  import { Tooltip } from "flowbite-svelte";
  import LiveIcon from "../assets/bufficon/live.webp";
  import SpeedIcon from "../assets/bufficon/speed.webp";
  import GuardIcon from "../assets/bufficon/guard.webp";
  import DynamicIcon from "../assets/bufficon/dynamic.webp";
  import WillIcon from "../assets/bufficon/will.webp";

  const nameToIcon = {
    live: {
      icon: LiveIcon,
      desc: "200%经验提升<直播>",
    },
    guard: {
      icon: GuardIcon,
      desc: "50%经验提升<舰长>",
    },
    dynamic: {
      icon: DynamicIcon,
      desc: "100%经验提升<动态>",
    },
  };

  export let type = "live";
</script>

<img width="24px" src={nameToIcon[type].icon} alt="" />
<Tooltip placement="bottom" class="origin-top scale-75"
  >{nameToIcon[type].desc}</Tooltip
>
