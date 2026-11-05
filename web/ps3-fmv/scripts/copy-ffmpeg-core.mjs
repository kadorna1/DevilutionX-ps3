import { copyFile, mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
// Vite's FFmpeg worker is a module worker and dynamically imports the core.
// The ESM build exports createFFmpegCore; the UMD build cannot be imported
// as a module and produces "failed to import ffmpeg-core.js".
const source = resolve(root, "node_modules/@ffmpeg/core/dist/esm");
const destination = resolve(root, "public/ffmpeg");
await mkdir(destination, { recursive: true });
for (const name of ["ffmpeg-core.js", "ffmpeg-core.wasm"]) {
  await copyFile(resolve(source, name), resolve(destination, name));
  console.log(`Copied local FFmpeg ESM asset: ${name}`);
}
