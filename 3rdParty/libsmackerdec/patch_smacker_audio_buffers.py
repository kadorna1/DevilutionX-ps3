#!/usr/bin/env python3
"""Safely size libsmackerdec audio buffers for the largest packet in a movie."""

from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_smacker_audio_buffers.py SmackerDecoder.cpp")

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str, description: str) -> None:
    global source
    if new in source:
        return
    count = source.count(old)
    if count != 1:
        raise SystemExit(
            f"error: expected exactly one upstream match for {description}; found {count}"
        )
    source = source.replace(old, new, 1)


old_scan = """\tuint32_t frameSize = frameSizes[0] & (~3);
\tuint8_t frameFlag  = frameFlags[0];

\t// skip over palette
\tif (frameFlag & kSMKpal)
\t{
\t\tuint32_t size = file.ReadByte();
\t\tsize = size * 4 - 1;
\t\tfile.Skip(size);
\t}

\tframeFlag >>= 1;

\tfor (int i = 0; i < kMaxAudioTracks; i++) {
\t\tif (frameFlag & 1) {
\t\t\t// skip size
\t\t\tfile.Skip(4);

\t\t\tuint32_t unpackedSize = file.ReadUint32LE();

\t\t\taudioTracks[i].bufferSize = unpackedSize;
\t\t\taudioTracks[i].buffer = new uint8_t[unpackedSize];
\t\t}
\t\tframeFlag >>= 1;
\t}
"""

new_scan = """\t// Determine the maximum uncompressed audio packet size across every
\t// frame. The first frame is not guaranteed to contain the largest packet.
\tstd::vector<uint32_t> maxAudioBufferSizes(kMaxAudioTracks, 0);
\tint32_t framePos = firstPos;
\tfor (uint32_t frameIndex = 0; frameIndex < nFrames; ++frameIndex) {
\t\tconst uint32_t frameSize = frameSizes[frameIndex] & (~3);
\t\tuint32_t remainingFrameBytes = frameSize;
\t\tuint8_t frameFlag = frameFlags[frameIndex];

\t\tif (frameSize == 0) {
\t\t\tSmackerCommon::LogError(\"Invalid zero-length Smacker frame\");
\t\t\treturn false;
\t\t}

\t\tfile.Seek(framePos, SmackerCommon::FileStream::kSeekStart);

\t\t// Skip the palette packet, including its one-byte size field.
\t\tif (frameFlag & kSMKpal) {
\t\t\tconst uint32_t paletteBytes = static_cast<uint32_t>(file.ReadByte()) * 4;
\t\t\tif (paletteBytes == 0 || paletteBytes > remainingFrameBytes) {
\t\t\t\tSmackerCommon::LogError(\"Invalid Smacker palette packet size\");
\t\t\t\treturn false;
\t\t\t}
\t\t\tfile.Skip(paletteBytes - 1);
\t\t\tremainingFrameBytes -= paletteBytes;
\t\t}

\t\tframeFlag >>= 1;
\t\tfor (int trackIndex = 0; trackIndex < kMaxAudioTracks; ++trackIndex) {
\t\t\tif (frameFlag & 1) {
\t\t\t\t// The packet size includes the uncompressed-size word and the
\t\t\t\t// remaining compressed data, but not this four-byte size field.
\t\t\t\tconst uint32_t packetSize = file.ReadUint32LE();
\t\t\t\tif (packetSize < 8 || packetSize > remainingFrameBytes) {
\t\t\t\t\tSmackerCommon::LogError(\"Invalid Smacker audio packet size\");
\t\t\t\t\treturn false;
\t\t\t\t}

\t\t\t\tconst uint32_t unpackedSize = file.ReadUint32LE();
\t\t\t\tmaxAudioBufferSizes[trackIndex] =
\t\t\t\t    std::max(maxAudioBufferSizes[trackIndex], unpackedSize);
\t\t\t\tfile.Skip(packetSize - 8);
\t\t\t\tremainingFrameBytes -= packetSize;
\t\t\t}
\t\t\tframeFlag >>= 1;
\t\t}

\t\tframePos += static_cast<int32_t>(frameSize);
\t}

\tfor (int trackIndex = 0; trackIndex < kMaxAudioTracks; ++trackIndex) {
\t\taudioTracks[trackIndex].bufferSize = maxAudioBufferSizes[trackIndex];
\t\tif (audioTracks[trackIndex].bufferSize != 0) {
\t\t\taudioTracks[trackIndex].buffer =
\t\t\t    new uint8_t[audioTracks[trackIndex].bufferSize];
\t\t}
\t}

\t// Restore the stream to the first frame for normal playback.
\tfile.Seek(firstPos, SmackerCommon::FileStream::kSeekStart);
"""

replace_once(old_scan, new_scan, "first-frame-only audio-buffer sizing")

old_guard = """    unpackedSize = bits.GetBits(32);

    if (!bits.GetBit()) {
"""
new_guard = """    unpackedSize = bits.GetBits(32);
    if (unpackedSize < 0 || static_cast<uint32_t>(unpackedSize) > track.bufferSize) {
        SmackerCommon::LogError(\"Smacker audio packet exceeds its allocated buffer\");
        track.bytesReadThisFrame = 0;
        return -1;
    }

    if (!bits.GetBit()) {
"""
replace_once(old_guard, new_guard, "audio decode capacity validation")

for required in (
    "maxAudioBufferSizes",
    "frameIndex < nFrames",
    "Smacker audio packet exceeds its allocated buffer",
):
    if required not in source:
        raise SystemExit(f"error: expected audio buffer safety change missing: {required}")

path.write_text(source, encoding="utf-8")
print("Patched libsmackerdec to scan every frame for maximum audio packet sizes and guard decoding capacity")
