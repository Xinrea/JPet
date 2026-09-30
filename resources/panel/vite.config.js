import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [svelte()],
  // Svelte 4 exposes the browser runtime through the `browser` export
  // condition. Keep it enabled for client builds; otherwise Vite 7 can
  // resolve `svelte` to its SSR runtime, where onMount is a no-op.
  resolve: {
    conditions: ['svelte', 'browser'],
  },
})
