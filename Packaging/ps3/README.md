# DevilutionX for PlayStation 3 (PSL1GHT)

This is an experimental CFW/HEN port using the PS3 SDL2 implementation from
[kadorna1/ps3dev-sdl2](https://github.com/kadorna1/ps3dev-sdl2). The build targets
the PPU with the ps3dev toolchain and uses SDL2's PSL1GHT RSX renderer.

## Build

The easiest reproducible environment is the existing PS3 SDL2 container:

```sh
docker run --rm -it \
  -v "$PWD:/workspace" \
  -w /workspace \
  hldtux/ps3dev-sdl2:latest bash
```

Inside the container, install the host-side build tools and build:

```sh
apt-get update
apt-get install -y cmake ninja-build gettext ccache python3
bash Packaging/ps3/build.sh
```

The build creates a PPU ELF and then wraps it as a PS3 NPDRM SELF
(`USRDIR/EBOOT.BIN`) inside `build-ps3/devilutionx-ps3.pkg`. When the SDK's
`package_finalize` utility is present, it also creates
`build-ps3/devilutionx-ps3-gnpdrm.pkg`. The packaging script validates the
SELF, PARAM.SFO and PKG headers/content ID before returning successfully.

GitHub Actions uploads two separate artifacts: `devilutionx-ps3-ppu-elf` for
the linked executable and built-in assets, and `devilutionx-ps3-pkg` for the
generated package(s). The workflow preserves the CMake/Ninja build tree plus
ccache between runs. Unchanged tracked source files have their checkout
timestamps normalized against a saved content-hash manifest, allowing Ninja
to rebuild only changed translation units.

## Install and game data

1. Install the generated PKG through the PS3 XMB.
2. Copy the original game's `DIABDAT.MPQ` to:

   `/dev_hdd0/game/DEVILUT00/USRDIR/DIABDAT.MPQ`

3. To use Hellfire, also copy the original expansion files
   `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, and `hfvoice.mpq`
   into the same `USRDIR` directory.

The PKG includes only the DevilutionX engine and its built-in redistributable
assets. It does not include Diablo or Hellfire game data. You must supply files
from a copy of the original game that you are entitled to use.

The PS3 build stores saves and configuration in the title's writable
`USRDIR` directory. The title ID is `DEVILUT00`.

## XMB media assets

The repository keeps PS3 XMB media in `assets/xmb/`. The generated package places these files beside `PARAM.SFO` at the package root:

- `ICON0.PNG` — the XMB static icon, 320×176 pixels.
- `PIC1.PNG` — XMB background, 1920×1080 pixels.
- `SND0.AT3` — XMB highlight sound; the supplied file is copied byte-for-byte.

`LOGO0.PNG` is retained in the repository but is unused and is not copied into the PKG.

## Startup log

The PS3 executable declares its process priority and 1 MiB main-thread stack with PSL1GHT's `SYS_PROCESS_PARAM` metadata. The linker explicitly retains `__sys_process_param` so the Release build's section garbage collection cannot discard the loader record.

The priority-103 C runtime constructor creates a persistent log using direct LV2 filesystem syscalls, before Newlib file I/O is initialized. Diagnostics are written only to the console's local HDD or mounted USB storage; no TTY or network logging is used.

It tries these paths in order:

1. `/dev_hdd0/game/DEVILUT00/USRDIR/devilutionx-ps3.log`
2. `/dev_hdd0/tmp/devilutionx-ps3.log`
3. `/dev_hdd0/game/DEVILUT00/devilutionx-ps3.log`
4. `/dev_usb000/devilutionx-ps3.log` through `/dev_usb005/devilutionx-ps3.log`

It uses the first writable location. Connect a USB drive before launching to allow a root-level log when the HDD targets cannot be opened. A missing log means the process did not reach the constructor or none of the local filesystem paths could be opened.

## Current porting status

This first bring-up target disables networking, uses the PlayStation gamepad
profile, enables the SDL2 renderer path, and uses unpacked save files. It is a
build-and-boot milestone, not yet a claim of verified in-game compatibility.
Graphics, DS3 input, sound, save/load behavior, and long-session stability still
need testing on RPCS3 and real PS3 hardware.
