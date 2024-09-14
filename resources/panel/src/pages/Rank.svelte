<script>
  const supabaseUrl = import.meta.env.VITE_SUPABASE_URL;
  const supabaseKey = import.meta.env.VITE_SUPABASE_KEY;
  import { Tabs, TabItem } from "flowbite-svelte";
  import {
    Button,
    Table,
    TableBody,
    TableBodyCell,
    TableBodyRow,
    TableHead,
    TableHeadCell,
  } from "flowbite-svelte";
  import { createClient } from "@supabase/supabase-js";
  export let account_info = {
    login: false,
    info: {
      confirm: false,
    }
  };
  export let attributes = null;
  export let starcnt = 0;
  const supabase = createClient(supabaseUrl, supabaseKey);
  let rank_data = [];
  let rank_star = [];
  let rank_exp = [];
  let rank_attr = [];

  function update() {
    supabase
      .from("rankboard")
      .select(
        "uid, name, ts, starcnt, speed, endurance, strength, will, intellect, exp",
      )
      .then((d) => {
        rank_data = d.data;
        rank_star = rank_data
          .map((e) => {
            return {
              name: e.name,
              starcnt: e.starcnt,
            };
          })
          .sort((a, b) => {
            return b.starcnt - a.starcnt;
          });
        rank_exp = rank_data
          .map((e) => {
            return {
              name: e.name,
              exp: e.exp,
            };
          })
          .sort((a, b) => {
            return b.exp - a.exp;
          });
        rank_attr = rank_data
          .map((e) => {
            return {
              name: e.name,
              attr: e.speed + e.endurance + e.strength + e.will + e.intellect,
            };
          })
          .sort((a, b) => {
            return b.attr - a.attr;
          });
      });
  }

  function confirm() {
    fetch(`/api/account/share`, { method: "POST" });
    account_info.info.confirm = true;
    push_data();
  }

  function push_data() {
    if(!attributes) {
      return;
    }
    supabase
      .from("rankboard")
      .upsert({
        uid: parseInt(account_info.info.uid),
        name: account_info.info.uname,
        starcnt: starcnt,
        speed: attributes.speed,
        endurance: attributes.endurance,
        strength: attributes.strength,
        will: attributes.will,
        intellect: attributes.intellect,
        exp: attributes.exp,
        ts: new Date().toISOString(),
      })
      .then(() => {
        update();
      });
  }

  let refresh = false;
  let last_refresh = parseInt(sessionStorage.getItem('last_refresh')) || 0;
  function update_data() {
    push_data();
    last_refresh = Date.now();
    sessionStorage.setItem("last_refresh", last_refresh.toString());
    refresh = true;
  }
  update();

  setInterval(()=>{
    if (account_info && account_info.info.confirm) {
      push_data();
    }
  }, 10 * 60 * 1000);

  setInterval(()=>{
    if (Date.now() - last_refresh >= 60 * 1000) {
      refresh = false;
    } else {
      refresh = true;
    }
  }, 1000);
</script>

{#if account_info}
  {#if !account_info.login}
    <p class="p-4">排行榜需要登录才能进行查看，请前往设置进行登录。</p>
  {:else if !account_info.info.confirm}
    <p class="p-4">
      查看排行榜将会认为你愿意共享游戏数据与部分账号数据(仅 uid
      和用户名)，数据将自动每 10 分钟进行一次同步，确认查看吗？
    </p>
    <Button class="w-full" on:click={confirm}>确认查看</Button>
  {:else}
    <Button class="w-full mb-2" on:click={update_data} disabled={refresh}>刷新排行榜</Button>
    <Tabs contentClass="bg-gray-50 rounded-lg dark:bg-gray-800 mt-2">
      <TabItem open title="星级榜">
        <Table>
          <TableHead>
            <TableHeadCell>排名</TableHeadCell>
            <TableHeadCell>用户名</TableHeadCell>
            <TableHeadCell>星级</TableHeadCell>
          </TableHead>
          <TableBody tableBodyClass="divide-y">
            {#each rank_star as item, i}
              <TableBodyRow>
                <TableBodyCell>{i + 1}</TableBodyCell>
                <TableBodyCell>{item.name}</TableBodyCell>
                <TableBodyCell>{item.starcnt}</TableBodyCell>
              </TableBodyRow>
            {/each}
          </TableBody>
        </Table>
      </TabItem>
      <TabItem title="经验榜">
        <Table>
          <TableHead>
            <TableHeadCell>排名</TableHeadCell>
            <TableHeadCell>用户名</TableHeadCell>
            <TableHeadCell>持有经验</TableHeadCell>
          </TableHead>
          <TableBody tableBodyClass="divide-y">
            {#each rank_exp as item, i}
              <TableBodyRow>
                <TableBodyCell>{i + 1}</TableBodyCell>
                <TableBodyCell>{item.name}</TableBodyCell>
                <TableBodyCell>{item.exp}</TableBodyCell>
              </TableBodyRow>
            {/each}
          </TableBody>
        </Table>
      </TabItem>
      <TabItem title="属性榜">
        <Table>
          <TableHead>
            <TableHeadCell>排名</TableHeadCell>
            <TableHeadCell>用户名</TableHeadCell>
            <TableHeadCell>总属性值</TableHeadCell>
          </TableHead>
          <TableBody tableBodyClass="divide-y">
            {#each rank_attr as item, i}
              <TableBodyRow>
                <TableBodyCell>{i + 1}</TableBodyCell>
                <TableBodyCell>{item.name}</TableBodyCell>
                <TableBodyCell>{item.attr}</TableBodyCell>
              </TableBodyRow>
            {/each}
          </TableBody>
        </Table>
      </TabItem>
    </Tabs>
  {/if}
{:else}
  加载中
{/if}
