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
    fail: {
      icon: FailIcon,
      desc: "50%经验提升<连败>"
    },
    monday: {
      icon: MondayIcon,
      desc: "50%经验提升<周一>"
    },
    birthday: {
      icon: BirthdayIcon,
      desc: "500%经验提升<生日>"
    }
  };

  export let type = "live";
</script>

<img width="24px" src={nameToIcon[type].icon} alt="" />
<Tooltip reference="#buff-ref" placement="bottom" arrow={false}>{nameToIcon[type].desc}</Tooltip>
