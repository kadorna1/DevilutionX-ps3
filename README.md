# DevilutionX — PlayStation 3 port

This repository branch is a PlayStation 3 port of [DevilutionX](https://github.com/diasurgical/DevilutionX), built for the **Cell Broadband Engine / PPU** with **PSL1GHT** and an SDL2 backend for PS3. The changes documented here are specific to the **ps3-port** branch.

The work covers the engine, platform layer, renderer, controller and media paths, PS3 executable/package pipeline, and CI. It is not a distribution of the original Diablo or Hellfire game data.

## Current release

- **Latest release:** [DevilutionX PS3 Port — 2026-11-06](https://github.com/kadorna1/DevilutionX-ps3/releases/tag/ps3-port-2026.11.06)
- **Branch:** [ps3-port](https://github.com/kadorna1/DevilutionX-ps3/tree/ps3-port)
- **Title ID:** **DEVILUT00**
- **Package:** **devilutionx-ps3-gnpdrm.pkg**

The release contains the installable PS3 package and a SHA-256 checksum. GitHub's source archives are generated from the release tag, which points to **ps3-port**. No Diablo/Hellfire MPQ archives are included.

## Installation and game data

1. Download the GNPDRM PKG from the [latest PS3 release](https://github.com/kadorna1/DevilutionX-ps3/releases/latest).
2. Install it through the PS3 XMB using a compatible CFW/HEN setup.
3. Copy your legally obtained original Diablo data file to:

   **/dev_hdd0/game/DEVILUT00/USRDIR/DIABDAT.MPQ**

4. For Hellfire, also provide the original **hellfire.mpq**, **hfmonk.mpq**, **hfmusic.mpq**, and **hfvoice.mpq** files in the same **USRDIR** directory.

The PKG contains the DevilutionX executable, PS3 runtime integration, and built-in redistributable engine assets. It does **not** contain **DIABDAT.MPQ**, Hellfire MPQs, or any other copyrighted game archive. The source tree also contains no **.MPQ** files. You must supply game data from a copy you are entitled to use.

### Optional PS3 FMV conversion

This branch includes a PS3 FMV preparation path for the original Smacker movies. The helper **Packaging/ps3/prepare_ps3_fmv_mpqs.py** can add H.264 Annex-B video, big-endian PCM audio, and metadata sidecars to copies of MPQ files. The browser-based converter is also published at [the PS3 FMV converter page](https://kadorna1.github.io/DevilutionX-ps3/).

The conversion tools process files supplied by the user. They do not include or distribute the source MPQs or the converted MPQ output. Keep the original archives backed up and do not redistribute converted archives because they contain original game data. Refer to [the PS3 packaging notes](Packaging/ps3/README.md) for the full command-line workflow and format details.

## PS3 engine and platform changes

The following summarizes the PS3-specific engine work present on this branch. The changes are guarded by PS3 build conditions where possible so the normal desktop and other console code paths remain separate.

### 1. PPU build target and platform integration

- Added a PS3 CMake toolchain and platform definition for the big-endian 64-bit PowerPC PPU target, using the PSL1GHT SDK toolchain.
- Added PS3-specific CMake dependency and SDL2 discovery paths, static SDL2 linking, and PS3 build options.
- Adjusted engine startup and platform initialization for the PS3 process model, runtime libraries, and available system modules.
- Added PS3 process metadata for application priority and main-thread stack size, and ensured the linker retains the metadata section in release builds.
- Added PSL1GHT ELF finalization using sprxlinker, including validation of the Cell LV2 ELF ABI, import stubs, and PPU function descriptors.
- Disabled unsupported or unnecessary desktop services in the PS3 build configuration, including network-dependent features and test targets that are not part of the console package.

### 2. SDL2, RSX rendering, and display

- Built SDL2 for PSL1GHT and added PS3-specific renderer, video, texture, and controller patches.
- Updated the SDL2/RSX transfer path to account for GPU alignment and pitch requirements, safe texture allocation, DMA completion, and GCM flip synchronization.
- Added safer RSX texture creation and lifetime handling, including allocation-failure checks and synchronization before resources are released.
- Added a direct RSX texture presentation path for PS3 FMV frames, with palette conversion caching for indexed video.
- Kept the game's 640×480 logical drawing canvas and explicitly scale it to the 1920×1080 physical output. The viewport is reset to the full output and separate X/Y scale factors are applied, so the image fills the screen instead of being aspect-fitted or letterboxed. This intentionally stretches the 4:3 logical canvas to 16:9.
- Force the PS3 display options needed for fullscreen/upscaled output and turn off Fit to Screen and integer scaling where they would otherwise constrain the image.

### 3. DualShock controller input

- Added a PS3 SDL2 joystick/controller backend patch for native PS3 controller discovery and input reporting.
- Connected the PS3 controller input path to the engine's game-control handling.
- Avoid loading touchscreen virtual-gamepad artwork on PS3. A physical DualShock controller is used instead; this skips the unnecessary overlay graphics initialization without disabling physical controller input.

### 4. Sound, Smacker movies, and Cell VDEC

- Added PS3-specific Smacker decoder integration through **3rdParty/libsmackerps3/** and the engine's movie/storm code.
- Added the PS3 Cell VDEC interface and a hardware video playback path for compatible H.264 streams prepared from original Smacker movies.
- Added an MPQ preparation utility that preserves original archive members while appending PS3-specific video/audio metadata sidecars to a user-created copy.
- Added direct RSX presentation for decoded FMV frames, palette caching, frame timing controls, and safer texture synchronization.
- Added PS3 FMV audio extraction/playback support using signed 16-bit big-endian PCM sidecars, along with packet-buffer safety fixes.
- Retained the original Smacker decoding path as a fallback when converted sidecars are absent or the VDEC path is not selected.
- Adapted the audio decoder integration and buffering for the PS3 runtime.

The release does not include movies, soundtracks, voice files, or other MPQ contents. The optional conversion workflow requires the user's own original game archives.

### 5. Filesystem, configuration, and saves

- Added PS3-aware path resolution and filesystem handling for the console's writable storage layout.
- Set the PS3 title ID and application data locations used by the engine and package.
- Enabled unpacked save handling on PS3 and added creation of the save/stash directories needed before the game writes save data.
- Added PS3-specific configuration behavior and diagnostics around reading and writing the engine configuration.
- Added a persistent startup log using direct LV2 filesystem calls so early startup errors can be recorded before the normal C/C++ file I/O runtime is available. It tries the game's HDD USRDIR, console temporary storage, and mounted USB devices.

### 6. Lua and PPU runtime compatibility

- Added PS3-specific Lua runtime initialization and protected-call handling.
- Fixed the Newlib/PPU AltiVec-sized setjmp buffer mismatch that could corrupt Lua protected-call state on PS3.
- Adjusted Lua state access and destruction order around the PS3 long-jump backend.
- Added targeted startup and allocation diagnostics to isolate PPU runtime failures without changing the non-PS3 Lua backend.

### 7. XMB application packaging

- Added the PS3 packaging scripts that create the PPU executable, finalized SELF/EBOOT.BIN, PARAM.SFO, and GNPDRM PKG.
- Added XMB media integration for ICON0.PNG, PIC1.PNG, and SND0.AT3. The provided AT3 sound is copied unchanged into the package.
- Added package checks for the PS3 PKG header, GNPDRM revision, title ID, executable format, and required process metadata.
- Kept the XMB assets and the installable package separate from user-supplied MPQ game data.

### 8. Build automation and diagnostics

- Added a Linux GitHub Actions workflow using the PSL1GHT/PS3 development container.
- Added persistent ccache and CMake/Ninja build-tree caching, plus a source-hash based incremental build helper.
- Added CI checks for the PPU ELF, Cell LV2 metadata, finalized SELF, and GNPDRM PKG.
- Published separate CI artifacts for the PPU ELF/built-in engine assets and the installable GNPDRM package.
- Added PS3-scoped diagnostics for boot, Lua setup, FMV decode/presentation, controller input, save/config I/O, RSX allocation, and level initialization.

## Building from source

The reproducible PS3 build environment is the **hldtux/ps3dev-sdl2:latest** container used by CI. From the repository root:

~~~sh
docker run --rm -it \
  -v "$PWD:/workspace" \
  -w /workspace \
  hldtux/ps3dev-sdl2:latest bash
~~~

Inside the container, install the host-side build dependencies and compile/package:

~~~sh
apt-get update
apt-get install -y cmake ninja-build gettext ccache python3 file binutils \
  autoconf automake libtool libtool-bin pkg-config git
bash Packaging/ps3/build.sh
~~~

For the SDL2 PSL1GHT renderer build, FMV conversion options, XMB asset layout, and logging locations, see [Packaging/ps3/README.md](Packaging/ps3/README.md).

The automated build is defined in [ps3-build.yml](.github/workflows/ps3-build.yml). It runs for pushes to **ps3-port** and can also be started manually from that branch.

## Branch scope and legal notes

This repository is a fork of DevilutionX, with this branch maintained as a PS3-specific port. The release is built from **ps3-port** only; it does not merge the fork's **master** branch as part of the release.

DevilutionX code remains subject to the repository's [LICENSE](LICENSE.md) and third-party notices. Diablo®, Hellfire, their game data, and related trademarks are the property of their respective owners. This project is not affiliated with or endorsed by Blizzard Entertainment. No copyrighted MPQ game data is provided.

---

**Original upstream README:** [README.original.md](README.original.md)
