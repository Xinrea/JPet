<script>
  import {
    ButtonGroup,
    Tooltip,
    Button,
    CheckboxButton,
  } from "flowbite-svelte";
  import PhotoIcon from "../assets/photo.svg";
  import imgClothes1 from "../assets/c1.png";
  import imgClothes2 from "../assets/c2.png";
  import imgClothes3 from "../assets/c3.png";
  import { onDestroy } from "svelte";
  import { sse } from "../sse.js";
  const clothesImages = [imgClothes1, imgClothes2, imgClothes3];

  export let current = 0;

  let parts_status = {
    ParamLEars: false,
    ParamREars: false,
    ParamHat: false,
    ParamGlasses: false,
    ParamEyeStar: false,
    ParamHair: false,
    ParamDizzy: false,
    ParamSweat: false,
    ParamBlackFace: false,
    ParamRedFace: false,
    ParamLegs: false,
    ParamShoes: false,
    ParamTail: false,
    ParamGun: false,
    ParamMouth1: false,
    ParamMouth2: false,
    ParamMouth3: false,
    ParamMouth4: false,
    ParamMouth5: false,
    ParamMouth6: false,
  };

  $: shorthair = !parts_status.ParamHair;

  function snapshot() {
    fetch("/api/snapshot", {
      method: "POST",
    });
  }

  function updatePartStatus() {
    fetch("/api/parts")
      .then((res) => res.json())
      .then((data) => {
        parts_status = data;
        console.log(data);
      });
  }
  /**
   * @param {string} param
   */
  function toggleParts(param) {
    fetch("/api/parts", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        param: param,
        enable: !parts_status[param],
      }),
    });
    parts_status[param] = !parts_status[param];
  }

  function toggleMouth(param) {
    fetch("/api/parts", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        param: param,
        enable: true,
      }),
    });
    parts_status[param] = true;
    for (let i = 1; i <= 6; i++) {
      let current = "ParamMouth" + String(i);
      // set other mouth to false
      if (current != param) {
        parts_status[current] = false;
      }
    }
  }

  updatePartStatus();
  const unsubscribe = sse.subscribe((event) => {
    if (event?.data === "SETTINGS_UPDATE") updatePartStatus();
  });
  onDestroy(unsubscribe);
</script>

<div class="dress-layout">
  <div class="dress-preview game-card"><span>今日造型</span><img src={clothesImages[current]} alt="轴伊的当前衣装" /><strong>轴伊 Joi</strong><p>用喜欢的装扮点亮日常</p></div>
  <div class="dress-grid"><section class="dress-card game-card"><h2>发型</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamHair}
    on:click={() => {
      toggleParts("ParamHair");
    }}>长发</CheckboxButton
  >
  <CheckboxButton
    bind:checked={shorthair}
    on:click={() => {
      toggleParts("ParamHair");
    }}>短发</CheckboxButton
  >
</ButtonGroup>
</section>
<section class="dress-card game-card"><h2>头饰</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamLEars}
    on:click={() => {
      toggleParts("ParamLEars");
    }}>左耳</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamREars}
    on:click={() => {
      toggleParts("ParamREars");
    }}>右耳</CheckboxButton
  >
  {#if current == 1}
    <CheckboxButton
      bind:checked={parts_status.ParamHat}
      on:click={() => {
        toggleParts("ParamHat");
      }}>贝雷帽</CheckboxButton
    >
  {/if}
</ButtonGroup>
</section>
<section class="dress-card game-card"><h2>眼部</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamDizzy}
    on:click={() => {
      toggleParts("ParamDizzy");
    }}>圈圈眼</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamEyeStar}
    on:click={() => {
      toggleParts("ParamEyeStar");
    }}>星星眼</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamGlasses}
    on:click={() => {
      toggleParts("ParamGlasses");
    }}>眼镜</CheckboxButton
  >
</ButtonGroup>
</section>
<section class="dress-card game-card"><h2>脸颊</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamSweat}
    on:click={() => {
      toggleParts("ParamSweat");
    }}>流汗</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamBlackFace}
    on:click={() => {
      toggleParts("ParamBlackFace");
    }}>脸黑</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamRedFace}
    on:click={() => {
      toggleParts("ParamRedFace");
    }}>脸红</CheckboxButton
  >
</ButtonGroup>
</section>
<section class="dress-card game-card"><h2>嘴型</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamMouth1}
    on:click={() => {
      toggleMouth("ParamMouth1");
    }}>嘴1</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamMouth2}
    on:click={() => {
      toggleMouth("ParamMouth2");
    }}>嘴2</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamMouth3}
    on:click={() => {
      toggleMouth("ParamMouth3");
    }}>嘴3</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamMouth4}
    on:click={() => {
      toggleMouth("ParamMouth4");
    }}>嘴4</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamMouth5}
    on:click={() => {
      toggleMouth("ParamMouth5");
    }}>嘴5</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamMouth6}
    on:click={() => {
      toggleMouth("ParamMouth6");
    }}>嘴6</CheckboxButton
  >
</ButtonGroup>
</section>
<section class="dress-card game-card"><h2>下身</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamLegs}
    on:click={() => {
      toggleParts("ParamLegs");
    }}>腿饰</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamShoes}
    on:click={() => {
      toggleParts("ParamShoes");
    }}>鞋子</CheckboxButton
  >
  <CheckboxButton
    bind:checked={parts_status.ParamTail}
    on:click={() => {
      toggleParts("ParamTail");
    }}>尾巴</CheckboxButton
  >
</ButtonGroup>
</section>
<section class="dress-card game-card"><h2>其它</h2>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamGun}
    on:click={() => {
      toggleParts("ParamGun");
    }}>枪</CheckboxButton
  >
</ButtonGroup>

</section></div>
</div>
<div class="dress-photo"><Button on:click={snapshot}><img src={PhotoIcon} height="20" width="20" alt="" /><span class="ml-2">拍张照</span></Button><span>保存一张当前角色的 PNG 截图</span></div>
<style>
  .dress-layout { display: grid; grid-template-columns: 185px minmax(0, 1fr); gap: 18px; align-items: start; }
  .dress-preview { position: sticky; top: 0; display: flex; flex-direction: column; align-items: center; padding: 20px 10px; background: repeating-linear-gradient(135deg, #ffffff55 0 12px, transparent 12px 24px), linear-gradient(#f4fae8, #fff); }
  .dress-preview > span { font-size: 11px; font-weight: 800; letter-spacing: 2px; color: #9cab7f; }
  .dress-preview > img { width: 115px; height: auto; margin: 18px 0; filter: drop-shadow(0 5px 2px #adc79122); }
  .dress-preview strong { font-size: 14px; font-weight: 900; color: var(--ink); }
  .dress-preview p { font-size: 10px; color: var(--muted); margin-top: 8px; }
  .dress-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 16px; }
  .dress-card { padding: 16px; min-width: 0; }
  .dress-card h2 { font-size: 13px; font-weight: 900; margin-bottom: 13px; padding-left: 9px; border-left: 3px solid #b7d986; }
  .dress-card :global(div.inline-flex) { display: flex; flex-wrap: wrap; gap: 8px; }
  .dress-card :global(label) { border-radius: 7px !important; margin: 0 !important; padding: 7px 11px; font-size: 12px; }
  .dress-card :global(label.text-primary-700) { background: #eff8e1; color: #619332; border-color: #aecd89; box-shadow: inset 0 0 0 1px #d8e8c0; }
  .dress-card :global(label:has(input:focus-visible)) { outline: 3px solid #84bb49; outline-offset: 3px; }
  .dress-photo { display: flex; flex-direction: column; align-items: center; gap: 12px; margin: 26px 0 8px; }
  .dress-photo > span { font-size: 10px; color: var(--muted); }
  @media (max-width: 680px) { .dress-layout { grid-template-columns: minmax(0, 1fr); } .dress-preview { position: relative; } .dress-preview > img { height: 165px; width: auto; } }
  @media (max-width: 460px) { .dress-grid { grid-template-columns: minmax(0, 1fr); gap: 12px; } }
</style>
