import { Archive, Creator } from "mopaq";
import { FFmpeg } from "@ffmpeg/ffmpeg";

const BASE_MOVIES = ["gendata\\logo.smk", "gendata\\diablo1.smk"];
const MAX_MOVIES = 32;
const encoder = new TextEncoder();
let ffmpeg = null;
let ffmpegReady = false;
let currentMovie = "";
let currentLogLines = null;

function send(type, payload = {}, transfer = []) {
  self.postMessage({ type, ...payload }, transfer);
}
function log(message) { send("log", { message }); }
function status(title, percent, detail) { send("status", { title, percent, detail }); }
function normalizeMember(value) { return String(value).replaceAll("/", "\\"); }
function movieStem(member) {
  const name = normalizeMember(member);
  const slash = name.lastIndexOf("\\");
  const dot = name.lastIndexOf(".");
  return name.slice(0, dot > slash ? dot : name.length);
}
function metadataFor(info) {
  return [
    "DVLX-PS3-VDEC-1",
    `source=${info.member.replaceAll("\\", "/")}`,
    `width=${info.width}`, `height=${info.height}`,
    `fps_num=${info.fpsNumerator}`, `fps_den=${info.fpsDenominator}`,
    `frame_count=${info.frameCount}`,
    `audio_rate=${info.audioRate}`, `audio_channels=${info.audioChannels}`,
    `video=${info.videoPath.replaceAll("\\", "/")}`,
    `audio=${info.audioRate ? info.audioPath.replaceAll("\\", "/") : "none"}`,
    "audio_format=s16be", "pixel_format=yuv420p", "bitstream=annexb-aud", ""
  ].join("\n");
}
function gcd(a, b) { while (b) [a, b] = [b, a % b]; return Math.abs(a) || 1; }
function rationalizeFps(value) {
  for (const denominator of [10000, 1000, 100, 1]) {
    let numerator = Math.round(value * denominator);
    const divisor = gcd(numerator, denominator);
    numerator /= divisor;
    const den = denominator / divisor;
    if (numerator > 0 && numerator <= 120000 && den > 0 && den <= 10000) return { numerator, denominator: den };
  }
  throw new Error(`Frame rate ${value} is outside PS3 metadata limits.`);
}
function countAudNals(bytes) {
  let count = 0;
  for (let i = 0; i + 4 < bytes.length;) {
    let nal = -1;
    if (bytes[i] === 0 && bytes[i+1] === 0 && bytes[i+2] === 0 && bytes[i+3] === 1) nal = i + 4;
    else if (bytes[i] === 0 && bytes[i+1] === 0 && bytes[i+2] === 1) nal = i + 3;
    if (nal < 0) { i++; continue; }
    if (nal < bytes.length && (bytes[nal] & 0x1f) === 9) count++;
    i = nal + 1;
  }
  return count;
}
function parseStreamDetails(lines) {
  const videoLine = lines.find((line) => /Video:/i.test(line) && /\d{2,5}x\d{2,5}/.test(line));
  if (!videoLine) throw new Error("FFmpeg did not report a readable Smacker video stream.");
  const dimensions = videoLine.match(/(\d{2,5})x(\d{2,5})/);
  const fps = videoLine.match(/([0-9]+(?:\.[0-9]+)?)\s*fps\b/i) || videoLine.match(/([0-9]+(?:\.[0-9]+)?)\s*tbr\b/i);
  if (!dimensions || !fps) throw new Error(`Unable to read video properties: ${videoLine}`);
  const audioLine = lines.find((line) => /Audio:/i.test(line));
  let audioRate = 0, audioChannels = 0;
  if (audioLine) {
    const rate = audioLine.match(/(\d{4,6})\s*Hz/i);
    if (rate) audioRate = Number(rate[1]);
    if (/\bmono\b/i.test(audioLine)) audioChannels = 1;
    else if (/\bstereo\b/i.test(audioLine)) audioChannels = 2;
    else {
      const channels = audioLine.match(/(\d+)\s*channels?/i);
      if (channels) audioChannels = Math.min(2, Number(channels[1]));
    }
    if (audioRate < 8000 || audioRate > 48000 || audioChannels < 1) throw new Error(`Unsupported Smacker audio format: ${audioLine}`);
  }
  const width = Number(dimensions[1]), height = Number(dimensions[2]);
  if (width > 1920 || height > 1080) throw new Error(`Resolution ${width}x${height} exceeds PS3 VDEC limit 1920x1080.`);
  const rate = rationalizeFps(Number(fps[1]));
  return { width, height, fpsNumerator: rate.numerator, fpsDenominator: rate.denominator, audioRate, audioChannels };
}
function findMovies(archive) {
  const candidates = new Map();
  for (const name of archive.files() ?? []) {
    if (String(name).toLowerCase().endsWith(".smk")) {
      const member = normalizeMember(name);
      candidates.set(member.toLowerCase(), member);
    }
  }
  for (const name of BASE_MOVIES) candidates.set(name.toLowerCase(), candidates.get(name.toLowerCase()) ?? name);
  return [...candidates.values()].slice(0, MAX_MOVIES);
}
async function verifyLocalAsset(url, label) {
  let response;
  try {
    // A HEAD request checks the Pages path without downloading the 32 MB WASM.
    // The service worker deliberately leaves HEAD requests untouched.
    response = await fetch(url, { method: "HEAD", cache: "no-store" });
  } catch (error) {
    throw new Error(`${label} request failed at ${url}: ${error?.message ?? String(error)}`);
  }
  if (!response.ok) throw new Error(`${label} returned HTTP ${response.status} at ${url}`);
  const size = response.headers.get("content-length");
  log(`${label} reachable (${size ? `${size} bytes` : "size not reported"}).`);
}

async function ensureFfmpeg() {
  if (ffmpegReady) return;
  status("Checking local FFmpeg assets", 3, "Verifying the FFmpeg module and WebAssembly paths on this GitHub Pages site.");
  const base = new URL(import.meta.env.BASE_URL, self.location.href);
  const coreURL = new URL("ffmpeg/ffmpeg-core.js", base).href;
  const wasmURL = new URL("ffmpeg/ffmpeg-core.wasm", base).href;
  await verifyLocalAsset(coreURL, "FFmpeg ESM core");
  await verifyLocalAsset(wasmURL, "FFmpeg WebAssembly core");

  status("Loading FFmpeg WebAssembly", 4, "Initializing the locally hosted codec core in a browser worker.");
  ffmpeg = new FFmpeg();
  ffmpeg.on("log", ({ message }) => {
    const line = String(message ?? "").trim();
    if (!line) return;
    if (currentLogLines) currentLogLines.push(line);
    if (/error|failed|Input #|Stream #|frame=|video:|audio:|Output #/i.test(line)) log(line);
  });
  ffmpeg.on("progress", ({ progress, time }) => send("progress", {
    title: currentMovie ? `Converting ${currentMovie}` : "Preparing codecs…",
    percent: Math.min(95, Math.max(5, 8 + (Number(progress) || 0) * 85)),
    detail: time ? `FFmpeg timestamp: ${time}` : "Processing locally in a browser worker."
  }));
  try {
    await ffmpeg.load({ coreURL, wasmURL });
  } catch (error) {
    throw new Error(`FFmpeg initialization failed after both local assets responded successfully: ${error?.message ?? String(error)}. Check the browser console for worker/CSP details.`);
  }
  currentLogLines = [];
  await ffmpeg.exec(["-hide_banner", "-decoders"]);
  const decoders = currentLogLines.join("\n");
  currentLogLines = [];
  await ffmpeg.exec(["-hide_banner", "-encoders"]);
  const encoders = currentLogLines.join("\n");
  currentLogLines = null;
  if (!/\bsmackvid\b/i.test(decoders) || !/\bsmackaud\b/i.test(decoders)) {
    throw new Error("The bundled FFmpeg core is missing Smacker video/audio decoders.");
  }
  if (!/\blibx264\b/i.test(encoders)) throw new Error("The bundled FFmpeg core is missing libx264.");
  ffmpegReady = true;
  log("FFmpeg core ready: Smacker video/audio decoders and libx264 encoder verified.");
}
async function convertMovie(member, smk, index, total) {
  currentMovie = member;
  status(`Extracting ${member}`, 8 + index / total * 78, "Reading original movie bytes from the local MPQ.");
  if (!(smk instanceof Uint8Array) || smk.length === 0) throw new Error(`Empty or unreadable MPQ member: ${member}`);
  const input = `/work/in-${index}.smk`;
  const video = `/work/out-${index}.h264`;
  const audio = `/work/out-${index}.pcm`;
  await ffmpeg.createDir("/work").catch(() => {});
  await ffmpeg.writeFile(input, smk);
  currentLogLines = [];
  const gop = 30;
  const result = await ffmpeg.exec([
    "-hide_banner", "-loglevel", "info", "-y", "-i", input,
    "-map", "0:v:0", "-an", "-sn", "-dn",
    "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency",
    "-pix_fmt", "yuv420p", "-profile:v", "baseline", "-level:v", "3.0",
    "-bf", "0", "-refs", "1", "-g", String(gop), "-keyint_min", String(gop), "-sc_threshold", "0",
    "-x264-params", `aud=1:repeat-headers=1:keyint=${gop}:min-keyint=${gop}:scenecut=0:bframes=0:ref=1:cabac=0:open-gop=0`,
    "-f", "h264", video
  ]);
  if (result !== 0) throw new Error(`H.264 conversion failed for ${member} (exit ${result}).`);
  const stream = parseStreamDetails(currentLogLines);
  const videoBytes = await ffmpeg.readFile(video);
  const frameCount = countAudNals(videoBytes);
  if (frameCount < 1) throw new Error(`No H.264 AUD frame delimiters generated for ${member}.`);
  let audioBytes = null;
  if (stream.audioRate && stream.audioChannels) {
    const audioResult = await ffmpeg.exec([
      "-hide_banner", "-loglevel", "error", "-y", "-i", input,
      "-map", "0:a:0", "-vn", "-sn", "-dn",
      "-c:a", "pcm_s16be", "-ar", String(stream.audioRate), "-ac", String(stream.audioChannels),
      "-f", "s16be", audio
    ]);
    if (audioResult !== 0) throw new Error(`PCM extraction failed for ${member} (exit ${audioResult}).`);
    audioBytes = await ffmpeg.readFile(audio);
    if (audioBytes.length === 0 || audioBytes.length % 2) throw new Error(`Invalid PCM output for ${member}.`);
  }
  const stem = movieStem(member);
  const info = {
    member, width: stream.width, height: stream.height,
    fpsNumerator: stream.fpsNumerator, fpsDenominator: stream.fpsDenominator,
    frameCount, audioRate: audioBytes ? stream.audioRate : 0, audioChannels: audioBytes ? stream.audioChannels : 0,
    videoPath: `${stem}.ps3.h264`, audioPath: `${stem}.ps3.pcm`, metaPath: `${stem}.ps3.meta`
  };
  await Promise.all([input, video, audio].map((path) => ffmpeg.deleteFile(path).catch(() => {})));
  log(`Converted ${member}: ${info.width}x${info.height}, ${info.fpsNumerator}/${info.fpsDenominator} fps, ${frameCount} frames, audio ${info.audioRate || "none"} Hz.`);
  return { info, videoBytes, audioBytes };
}
async function processArchives(files) {
  if (!files.length) throw new Error("Select at least one MPQ archive.");
  await ensureFfmpeg();
  const creator = new Creator({ sectorSize: 65536, listfile: true });
  const seen = new Set();
  let converted = 0;
  let movieIndex = 0;

  // Inspect each archive independently. A Hellfire MPQ selected before DIABDAT
  // must not claim the base game's known intro paths if those files are absent.
  for (const file of files) {
    if (!file.size) throw new Error(`${file.name} is empty.`);
    if (file.size > 1024 * 1024 * 1024) throw new Error(`${file.name} exceeds the 1 GiB browser safety limit.`);
    status(`Opening ${file.name}`, 5, "Opening the selected archive directly from the local file picker.");
    let archive;
    try {
      archive = Archive.open(new Uint8Array(await file.arrayBuffer()));
    } catch (error) {
      throw new Error(`Cannot open ${file.name}: ${error.message}`);
    }

    const members = findMovies(archive);
    log(`${file.name}: ${members.length} candidate Smacker path(s).`);
    for (const member of members) {
      const key = member.toLowerCase();
      if (seen.has(key)) continue;

      let smk;
      try {
        smk = archive.readFile(member);
      } catch (error) {
        log(`Not present/readable in ${file.name}: ${member} (${error.message})`);
        continue;
      }
      if (!(smk instanceof Uint8Array) || smk.length === 0) {
        log(`Skipping empty movie member: ${member} in ${file.name}`);
        continue;
      }

      seen.add(key);
      try {
        log(`Processing ${member} from ${file.name}.`);
        const { info, videoBytes, audioBytes } = await convertMovie(member, smk, movieIndex++, MAX_MOVIES);
        creator.addFile(info.videoPath, videoBytes, { compress: false });
        if (audioBytes) creator.addFile(info.audioPath, audioBytes, { compress: true });
        creator.addFile(info.metaPath, encoder.encode(metadataFor(info)), { compress: true });
        converted++;
      } catch (error) {
        log(`Skipped ${member}: ${error.message}`);
        if (key === "gendata\\\\diablo1.smk") throw error;
      }
      if (converted > MAX_MOVIES) throw new Error(`More than ${MAX_MOVIES} movies were found; split the archives into smaller batches.`);
    }
  }

  if (!converted) throw new Error("No Smacker movies could be converted. Make sure DIABDAT.MPQ is selected.");
  status("Writing the VDEC sidecar MPQ", 96, "Creating a separate MPQ in browser memory.");
  const output = creator.write();
  const validation = Archive.open(output);
  const listed = validation.files() ?? [];
  if (!listed.length) throw new Error("The generated sidecar MPQ has no readable listfile.");
  for (const entry of listed.filter((name) => String(name).toLowerCase().endsWith(".ps3.meta"))) {
    if (!new TextDecoder().decode(validation.readFile(entry)).startsWith("DVLX-PS3-VDEC-1")) {
      throw new Error(`Metadata validation failed for ${entry}.`);
    }
  }
  const transferable = output.buffer.slice(output.byteOffset, output.byteOffset + output.byteLength);
  send("complete", { filename: "devilutionx-vdec.mpq", movieCount: converted, buffer: transferable }, [transferable]);
}

self.addEventListener("message", async (event) => {
  const message = event.data ?? {};
  if (message.type === "clear") {
    if (ffmpeg) { ffmpeg.terminate(); ffmpeg = null; ffmpegReady = false; }
    return;
  }
  if (message.type !== "convert") return;
  try { await processArchives(message.files ?? []); }
  catch (error) { send("error", { message: error?.message ?? String(error) }); }
  finally { currentMovie = ""; currentLogLines = null; }
});
