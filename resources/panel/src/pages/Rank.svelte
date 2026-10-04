<script>
  import { onMount } from "svelte";
  import { Button, Table, TableBody, TableBodyCell, TableBodyRow, TableHead, TableHeadCell } from "flowbite-svelte";
  export let account_info = null;
  const boards = [{ key: "starcnt", title: "星级榜", label: "星级" }, { key: "exp", title: "经验榜", label: "持有经验" }, { key: "attr", title: "属性榜", label: "总属性值" }];
  let metric = "starcnt", entries = [], me = null, offset = 0, loading = false, error = "";
  $: board = boards.find(b => b.key === metric);
  async function load() {
    if (loading) return;
    loading = true; error = "";
    try {
      const response = await fetch(`/api/rank?metric=${metric}&offset=${offset}`);
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "排行榜加载失败");
      entries = data.entries; me = data.me;
    } catch (failure) { error = failure.message; }
    finally { loading = false; }
  }
  async function confirm(enabled = true) {
    error = "";
    try {
      const response = await fetch("/api/account/share", { method: enabled ? "POST" : "DELETE" });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "参与排行榜失败");
      account_info.info.confirm = enabled;
      await load();
    } catch (failure) { error = failure.message; }
  }
  function select(key) { metric = key; offset = 0; load(); }
  function page(direction) { offset = Math.max(0, offset + direction * 100); load(); }
  onMount(load);
</script>

{#if error}<p class="mb-3 rounded bg-red-50 p-3 text-sm text-red-600" role="alert">{error}</p>{/if}
{#if account_info?.login && !account_info.info.confirm}
  <p class="mb-3 text-sm text-gray-600">参与排行榜会公开你的 UID、用户名、星级、持有经验和总属性。排行由云端存档生成。</p>
  <Button class="mb-4 w-full" disabled={loading} on:click={() => confirm(true)}>参与排行榜</Button>
{:else if account_info?.login && account_info.info.confirm}
  <Button class="mb-4" size="xs" color="alternative" disabled={loading} on:click={() => confirm(false)}>退出排行榜</Button>
{:else if !account_info?.login}
  <p class="mb-3 text-sm text-gray-500">登录并连接云端后，可以参与排行榜。</p>
{/if}
<div class="mb-3 flex gap-2">
  {#each boards as item}<Button color={metric === item.key ? "primary" : "alternative"} size="sm" disabled={loading} on:click={() => select(item.key)}>{item.title}</Button>{/each}
</div>
<div class="mb-3 flex items-center justify-between gap-2">
  <span class="text-sm text-gray-600">{me ? `我的排名：${me.rank} · ${board.label}：${me.value}` : "暂未上榜"}</span>
  <Button size="xs" color="alternative" disabled={loading} on:click={load}>{loading ? "加载中…" : "刷新"}</Button>
</div>
<Table>
  <TableHead><TableHeadCell>排名</TableHeadCell><TableHeadCell>用户名</TableHeadCell><TableHeadCell>{board.label}</TableHeadCell></TableHead>
  <TableBody tableBodyClass="divide-y">
    {#each entries as item, i}
      <TableBodyRow><TableBodyCell>{offset + i + 1}</TableBodyCell><TableBodyCell>{item.name}</TableBodyCell><TableBodyCell>{item.value}</TableBodyCell></TableBodyRow>
    {/each}
  </TableBody>
</Table>
{#if !loading && entries.length === 0}<p class="p-4 text-sm text-gray-500">暂无排行数据。</p>{/if}
<div class="mt-3 flex justify-between">
  <Button size="xs" color="alternative" disabled={loading || offset === 0} on:click={() => page(-1)}>上一页</Button>
  <Button size="xs" color="alternative" disabled={loading || entries.length < 100 || offset >= 10000} on:click={() => page(1)}>下一页</Button>
</div>
