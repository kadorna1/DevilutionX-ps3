import { defineConfig } from "vite";

export default defineConfig({
  base: process.env.GITHUB_ACTIONS ? "/DevilutionX-ps3/" : "/",
  publicDir: "public",
  build: { target: "es2022", sourcemap: false, emptyOutDir: true },
  worker: { format: "es" }
});
