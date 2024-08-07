<script>
  import { Label, ButtonGroup, CheckboxButton } from "flowbite-svelte";

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
</script>

<Label class="mb-2">发型</Label>
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
<Label class="mt-4 mb-2">头饰</Label>
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
<Label class="mt-4 mb-2">眼部</Label>
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
<Label class="mt-4 mb-2">脸颊</Label>
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
<Label class="mt-4 mb-2">嘴型</Label>
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
<Label class="mt-4 mb-2">下身</Label>
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
<Label class="mt-4 mb-2">其它</Label>
<ButtonGroup>
  <CheckboxButton
    bind:checked={parts_status.ParamGun}
    on:click={() => {
      toggleParts("ParamGun");
    }}>枪</CheckboxButton
  >
</ButtonGroup>
