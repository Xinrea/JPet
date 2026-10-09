import { cloudflareTest } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [cloudflareTest({ wrangler: { configPath: "./wrangler.jsonc" }, miniflare: { bindings: { QWEN_WORKSPACE_ID: "test-space", QWEN_API_KEY: "test-key" } } })],
  test: { fileParallelism: false },
});
