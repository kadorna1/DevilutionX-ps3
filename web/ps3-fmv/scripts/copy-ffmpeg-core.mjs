import { copyFile, mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const source = resolve(root, "node_modules/@ffmpeg/core/dist/umd");
const destination = resolve(root, "public/ffmpeg");
await mkdir(destination, { recursive: true });
for (const name of ["ffmpeg-core.js", "ffmpeg-core.wasm"]) {
  await copyFile(resolve(source, name), resolve(destination, name));
  console.log(`Copied local FFmpeg asset: ${name}`);
}
