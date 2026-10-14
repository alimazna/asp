import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));

const r = (p: string) => path.resolve(root, p);

// https://vitejs.dev/config/
// ASTRA web terminal. The API base URL comes from the environment so the same
// build can point at the local AURA loopback API or a future HTTPS gateway.
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      api: r("src/api"),
      components: r("src/components"),
      charts: r("src/charts"),
      layouts: r("src/layouts"),
      lib: r("src/lib"),
      pages: r("src/pages"),
      styles: r("src/styles"),
    },
  },
  server: {
    port: 5173,
    host: true,
    /**
     * Dev-only proxy. The AURA backend (and the T19 mock) is loopback-only and
     * sends no CORS headers, so a browser on :5173 cannot call it cross-origin.
     * Proxying /api keeps dev requests same-origin. Point the app at it with
     * VITE_API_BASE_URL=/api/v1/ . This block has no effect on `vite build` and
     * is NOT a substitute for the production HTTPS gateway.
     */
    proxy: {
      "/api": {
        target: process.env.VITE_DEV_API_TARGET ?? "http://127.0.0.1:8790",
        changeOrigin: true,
      },
    },
  },
  preview: {
    port: 4173,
    host: true,
  },
  build: {
    target: "es2020",
    sourcemap: false,
    outDir: "dist",
  },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["./tests/setup.ts"],
    include: ["tests/**/*.test.{ts,tsx}"],
    css: false,
  },
});
