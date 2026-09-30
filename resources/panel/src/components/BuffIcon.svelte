<script>
  import { Tooltip } from "flowbite-svelte";
  import LiveIcon from "../assets/bufficon/live.webp";
  import GuardIcon from "../assets/bufficon/guard.webp";
  import DynamicIcon from "../assets/bufficon/dynamic.webp";
  import FailIcon from "../assets/bufficon/fail.webp";
  import MondayIcon from "../assets/bufficon/monday.webp";
  import BirthdayIcon from "../assets/bufficon/birthday.webp";

  const nameToIcon = {
    live: {
      icon: LiveIcon,
      desc: "100%经验提升<直播>",
    },
    dynamic: {
      icon: DynamicIcon,
      desc: "50%经验提升<动态>",
    },
    guard: {
      icon: GuardIcon,
      desc: "25%经验提升<舰长>",
    },
    fail: {
      icon: FailIcon,
      desc: "25%经验提升<连败>"
    },
    monday: {
      icon: MondayIcon,
      desc: "25%经验提升<周一>"
    },
    birthday: {
      icon: BirthdayIcon,
      desc: "250%经验提升<生日>"
    }
  };

  export let type = "live";
</script>

<img
  class="buff-icon"
  width="24"
  height="32"
  src={nameToIcon[type].icon}
  alt=""
/>
<Tooltip reference="#buff-ref" placement="bottom" arrow={false}>{nameToIcon[type].desc}</Tooltip>

<style>
  .buff-icon {
    display: block;
    flex: 0 0 auto;
    width: 24px;
    height: 32px;
  }
</style>
