<script>
  import "./app.css";
  import { Tabs, TabItem } from "flowbite-svelte";
  import Profile from "./pages/Profile.svelte";
  import Document from "./pages/Document.svelte";
  import Setting from "./pages/Setting.svelte";
</script>

<main>
  <Tabs tabStyle="underline">
    <TabItem open title="总览">
      <Profile />
    </TabItem>
    <TabItem title="任务"></TabItem>
    <TabItem title="自定义">
      <p class="text-sm text-gray-500 dark:text-gray-400">
        <b>Dashboard:</b>
        Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor
        incididunt ut labore et dolore magna aliqua.
      </p>
    </TabItem>
    <TabItem title="设置">
      <Setting />
    </TabItem>
    <TabItem title="等级说明">
      <Document />
    </TabItem>
  </Tabs>
</main>

<style>
</style>
