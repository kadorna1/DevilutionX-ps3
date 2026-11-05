# Browser-only PS3 FMV converter

This static GitHub Pages app converts user-supplied Diablo/Hellfire MPQs to a **separate** `devilutionx-vdec.mpq` archive for PS3 Cell VDEC playback.

## Privacy

- MPQs are selected using the browser File API and read in browser memory.
- MPQ parsing, Smacker decoding, H.264 encoding, PCM extraction and MPQ creation run in a Web Worker/WebAssembly.
- The app never uploads archive contents, filenames, logs or output. There is no backend, analytics, telemetry, account, remote storage or third-party runtime CDN.
- The service worker caches application code and codec assets only. It does not cache selected MPQs or generated archives.
- Clearing the page removes file references and terminates the FFmpeg worker. The output is a local Blob download.

GitHub Pages has to deliver the static app code to the browser. Those application assets are cached locally; the input game archives themselves never leave the computer running the browser.

## Output

The converter creates a small MPQ v1 named `devilutionx-vdec.mpq`, containing sidecars such as `gendata\\diablo1.ps3.h264`, `gendata\\diablo1.ps3.pcm` and `gendata\\diablo1.ps3.meta`. The metadata uses `DVLX-PS3-VDEC-1`.

The PS3 engine loads this optional archive separately at higher priority. Original `DIABDAT.MPQ` and Hellfire archives are not rewritten.

## Local build

Requires Node.js 20+:

```sh
npm install
npm run build
npm run preview
```

Build copies the pinned FFmpeg core ESM assets into `public/ffmpeg`. The ESM build is required because the FFmpeg worker is a module worker; runtime requests stay on the GitHub Pages origin.

## Requirements and limits

A modern browser with WebAssembly, ES modules, Web Workers, Blob downloads and Service Worker support. The single-thread FFmpeg core avoids SharedArrayBuffer/cross-origin-isolation requirements. Conversion uses substantial RAM and CPU. Each MPQ is limited to 1 GiB by the interface.

MPQ reading uses `mopaq` (MPQ v1; zlib, PKWARE DCL, Huffman and IMA ADPCM reads). MPQ v2-v4 and single-unit members are unsupported. The bundled FFmpeg core is checked for Smacker video/audio decoding and `libx264` before conversion.

See `THIRD-PARTY-NOTICES.md` for licenses.
