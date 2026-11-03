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

## Startup log

Every PS3 launch recreates a plain-text startup log at:

`/dev_hdd0/game/DEVILUT00/USRDIR/devilutionx-ps3.log`

The log captures early process output, SDL messages, and markers around the main startup stages. Messages are flushed as they are written so the file remains useful when the title exits or crashes. Retrieve it from the PS3 HDD with an FTP client or a homebrew file manager. The last `PS3 boot:` entry identifies the last startup stage reached. If the file is not created at all, the executable likely did not reach `main()` (or the title directory could not be opened for writing).

## Current porting status

This first bring-up target disables networking, uses the PlayStation gamepad
profile, enables the SDL2 renderer path, and uses unpacked save files. It is a
build-and-boot milestone, not yet a claim of verified in-game compatibility.
Graphics, DS3 input, sound, save/load behavior, and long-session stability still
need testing on RPCS3 and real PS3 hardware.
