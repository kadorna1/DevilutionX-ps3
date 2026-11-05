import "./style.css";

const $ = (selector) => document.querySelector(selector);
const input = $("#mpq-files");
const dropzone = $("#dropzone");
const fileList = $("#file-list");
const convertButton = $("#convert");
const clearButton = $("#clear");
const progressPanel = $("#progress-panel");
const progressTitle = $("#progress-title");
const progressPercent = $("#progress-percent");
const progressBar = $("#progress-bar");
const progressDetail = $("#progress-detail");
const resultPanel = $("#result-panel");
const resultDetail = $("#result-detail");
const downloadLink = $("#download");
const logElement = $("#log");
let selectedFiles = [];
let worker = null;
let busy = false;
let outputUrl = null;

function formatBytes(value) {
  return value < 1024 * 1024 ? `${(value / 1024).toFixed(1)} KB` : `${(value / 1048576).toFixed(1)} MB`;
}
function appendLog(message) {
  const line = String(message ?? "").trim();
  if (!line) return;
  const current = logElement.textContent === "Waiting for MPQ files." ? "" : logElement.textContent;
  logElement.textContent = `${current}${current ? "\n" : ""}${line}`.slice(-18000);
  logElement.scrollTop = logElement.scrollHeight;
}
function setProgress(title, percent, detail = "") {
  progressPanel.hidden = false;
  progressTitle.textContent = title;
  const value = Math.max(0, Math.min(100, Number(percent) || 0));
  progressPercent.textContent = `${Math.round(value)}%`;
  progressBar.style.width = `${value}%`;
  progressDetail.textContent = detail;
}
function revokeOutput() {
  if (outputUrl) URL.revokeObjectURL(outputUrl);
  outputUrl = null;
}
function refreshFiles() {
  fileList.replaceChildren();
  fileList.classList.toggle("empty", selectedFiles.length === 0);
  if (selectedFiles.length === 0) fileList.textContent = "No archives selected.";
  for (const file of selectedFiles) {
    const row = document.createElement("div");
    row.className = "file-item";
    const name = document.createElement("strong");
    name.textContent = file.name;
    const size = document.createElement("span");
    size.textContent = formatBytes(file.size);
    row.append(name, size);
    fileList.append(row);
  }
  convertButton.disabled = busy || selectedFiles.length === 0;
  clearButton.disabled = busy || (selectedFiles.length === 0 && !outputUrl);
}
function setFiles(files) {
  const all = [...files];
  selectedFiles = all.filter((file) => file.name.toLowerCase().endsWith(".mpq"));
  if (selectedFiles.length !== all.length) appendLog("Only .mpq files are accepted.");
  resultPanel.hidden = true;
  revokeOutput();
  refreshFiles();
  if (selectedFiles.length) appendLog(`Selected ${selectedFiles.length} MPQ archive(s). Files remain local to this browser.`);
}
input.addEventListener("change", () => setFiles(input.files ?? []));
for (const name of ["dragenter", "dragover"]) dropzone.addEventListener(name, (event) => {
  event.preventDefault(); dropzone.classList.add("dragging");
});
for (const name of ["dragleave", "drop"]) dropzone.addEventListener(name, (event) => {
  event.preventDefault(); dropzone.classList.remove("dragging");
});
dropzone.addEventListener("drop", (event) => setFiles(event.dataTransfer?.files ?? []));

function finishBusy() {
  busy = false;
  refreshFiles();
}
function onWorkerMessage(event) {
  const message = event.data ?? {};
  if (message.type === "log") appendLog(message.message);
  if (message.type === "status" || message.type === "progress") {
    setProgress(message.title, message.percent, message.detail);
  }
  if (message.type === "error") {
    appendLog(`ERROR: ${message.message}`);
    progressTitle.textContent = "Conversion stopped";
    progressDetail.textContent = "No output archive was created. Review the log above.";
    finishBusy();
  }
  if (message.type === "complete") {
    const blob = new Blob([message.buffer], { type: "application/octet-stream" });
    revokeOutput();
    outputUrl = URL.createObjectURL(blob);
    downloadLink.href = outputUrl;
    downloadLink.download = message.filename || "devilutionx-vdec.mpq";
    resultDetail.textContent = `${message.movieCount} movie(s) · ${formatBytes(blob.size)} · generated only in this browser`;
    resultPanel.hidden = false;
    setProgress("Conversion complete", 100, "Download the sidecar MPQ to your computer.");
    appendLog(`Created ${downloadLink.download} (${formatBytes(blob.size)}). Original MPQs were not changed.`);
    finishBusy();
  }
}
function getWorker() {
  if (worker) return worker;
  worker = new Worker(new URL("./processor.worker.js", import.meta.url), { type: "module" });
  worker.addEventListener("message", onWorkerMessage);
  worker.addEventListener("error", (event) => {
    appendLog(`Worker error: ${event.message || "unknown error"}`);
    finishBusy();
  });
  return worker;
}
convertButton.addEventListener("click", () => {
  if (busy || selectedFiles.length === 0) return;
  resultPanel.hidden = true;
  revokeOutput();
  logElement.textContent = "";
  busy = true;
  refreshFiles();
  setProgress("Starting local converter…", 0, "No archive data is sent over the network.");
  appendLog("Starting browser-only conversion.");
  getWorker().postMessage({ type: "convert", files: selectedFiles });
});
clearButton.addEventListener("click", () => {
  if (busy) return;
  selectedFiles = [];
  input.value = "";
  revokeOutput();
  resultPanel.hidden = true;
  progressPanel.hidden = true;
  logElement.textContent = "Browser references cleared. No source files were written to disk by this tool.";
  if (worker) worker.postMessage({ type: "clear" });
  refreshFiles();
});
if ("serviceWorker" in navigator && location.protocol.startsWith("http")) {
  window.addEventListener("load", () => {
    // import.meta.env.BASE_URL is a path (for example /DevilutionX-ps3/),
    // so resolve it against the current origin before constructing sw.js.
    const appBase = new URL(import.meta.env.BASE_URL, location.origin);
    navigator.serviceWorker.register(new URL("sw.js", appBase), { scope: appBase.pathname })
      .then(() => appendLog("Offline app assets are available through the browser cache."))
      .catch((error) => appendLog(`Offline cache registration unavailable: ${error.message}`));
  });
}
refreshFiles();
