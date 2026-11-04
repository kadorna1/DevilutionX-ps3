# DevilutionX for PlayStation 3 (PSL1GHT)

This is an experimental CFW/HEN port using the PS3 SDL2 implementation from
[kadorna1/ps3dev-sdl2](https://github.com/kadorna1/ps3dev-sdl2). The build targets
the PPU with the ps3dev toolchain and uses SDL2's PSL1GHT RSX renderer.

## Build

The easiest reproducible environment is the existing PS3 SDL2 container:

~~~sh
docker run --rm -it \
  -v "$PWD:/workspace" \
  -w /workspace \
  hldtux/ps3dev-sdl2:latest bash
~~~

Inside the container, install the host-side build tools and build:

~~~sh
apt-get update
apt-get install -y cmake ninja-build gettext ccache python3
bash Packaging/ps3/build.sh
~~~

The build creates a PPU ELF and then wraps it as a PS3 NPDRM SELF
(USRDIR/EBOOT.BIN) inside build-ps3/devilutionx-ps3.pkg. When the SDK's
package_finalize utility is present, it also creates
build-ps3/devilutionx-ps3-gnpdrm.pkg. The packaging script validates the
SELF, PARAM.SFO and PKG headers/content ID before returning successfully.

GitHub Actions uploads two separate artifacts: devilutionx-ps3-ppu-elf for
the linked executable and built-in assets, and devilutionx-ps3-pkg for the
generated package(s). The workflow preserves the CMake/Ninja build tree plus
ccache between runs. Unchanged tracked source files have their checkout
timestamps normalized against a saved content-hash manifest, allowing Ninja
to rebuild only changed translation units.

## Install and game data

1. Install the generated PKG through the PS3 XMB.
2. Copy the original game's DIABDAT.MPQ to:

   /dev_hdd0/game/DEVILUT00/USRDIR/DIABDAT.MPQ

3. To use Hellfire, also copy the original expansion files
   hellfire.mpq, hfmonk.mpq, hfmusic.mpq, and hfvoice.mpq
   into the same USRDIR directory.

The PKG includes only the DevilutionX engine and its built-in redistributable
assets. It does not include Diablo or Hellfire game data. You must supply files
from a copy of the original game that you are entitled to use.

The PS3 build stores saves and configuration in the title's writable
USRDIR directory. The title ID is DEVILUT00.

## Preparing MPQs for PS3 Cell VDEC FMVs

The PS3 movie code now prefers Cell VDEC hardware playback when converted
sidecars are available in the game MPQs. Cell VDEC accepts H.264/MPEG-2 access
units, not Smacker packets, so the host-side helper converts the Smacker video
to H.264 Annex-B (Constrained Baseline, 8-bit 4:2:0, no B-frames, an AUD
delimiter for each frame) and extracts the audio as raw signed 16-bit
big-endian PCM. The PPU can consume the PCM as native int16 samples.

The helper is Packaging/ps3/prepare_ps3_fmv_mpqs.py. It appends
.ps3.h264, .ps3.pcm and .ps3.meta sidecar members to copies of the original
MPQs. Original members—including the original .smk files and all unrelated
game data—are retained, and the original MPQ files are never modified.

On Ubuntu, install the host tools:

~~~sh
sudo apt-get update
sudo apt-get install -y smpq ffmpeg
~~~

Convert every MPQ in a directory (including any Hellfire archives):

~~~sh
python3 Packaging/ps3/prepare_ps3_fmv_mpqs.py \
  --mpq-dir "/path/to/Diablo game data" \
  --output-dir "./ps3-fmv-mpqs"
~~~

Or pass individual archives:

~~~sh
python3 Packaging/ps3/prepare_ps3_fmv_mpqs.py \
  --mpq "/path/to/DIABDAT.MPQ" \
  --mpq "/path/to/hellfire.mpq" \
  --mpq "/path/to/hfmonk.mpq" \
  --mpq "/path/to/hfmusic.mpq" \
  --mpq "/path/to/hfvoice.mpq" \
  --output-dir "./ps3-fmv-mpqs"
~~~

The helper always attempts gendata/logo.smk and gendata/diablo1.smk. It also
converts other .smk members it can identify from the archive listing. For MPQs
without a usable listfile, pass a compatible StormLib listfile with --listfile
/path/to/listfile.txt. The generated archive copies keep the original archive
filenames. Back up the originals, then copy the converted MPQ files into
USRDIR, replacing the matching original files. Do not distribute converted
archives: they contain original game data.

When the converted sidecars are missing, the movie player falls back to the
existing Smacker decoder. The VDEC path is newly integrated and still requires
real-console playback validation; a successful CI build does not prove that
the PS3 firmware decoder accepts every stream or that frame timing is perfect.

## XMB media assets

The repository keeps PS3 XMB media in assets/xmb/. The generated package places
these files beside PARAM.SFO at the package root:

- ICON0.PNG — the XMB static icon, 320×176 pixels.
- PIC1.PNG — XMB background, 1920×1080 pixels.
- SND0.AT3 — XMB highlight sound; the supplied file is copied byte-for-byte.

LOGO0.PNG is retained in the repository but is unused and is not copied into the PKG.

## PS3 ELF finalization

Before creating EBOOT.BIN, the packaging script runs PSL1GHT's sprxlinker on
the stripped PPU executable. This is part of the normal PSL1GHT application
pipeline: it marks the executable with the Cell LV2 ELF OSABI, repairs
import-stub counts, and fixes PPU function descriptors. Packaging stops unless
the result is a 64-bit big-endian PowerPC executable with ELFOSABI_CELLLV2 (102).

## Startup log

The PS3 executable declares its process priority and 1 MiB main-thread stack
with PSL1GHT's SYS_PROCESS_PARAM metadata. The linker explicitly retains
__sys_process_param so the Release build's section garbage collection cannot
discard the loader record.

The priority-103 C runtime constructor creates a persistent log using direct
LV2 filesystem syscalls, before Newlib file I/O is initialized. Diagnostics
are written exclusively to the console's local HDD or mounted USB storage.

It tries these paths in order:

1. /dev_hdd0/game/DEVILUT00/USRDIR/devilutionx-ps3.log
2. /dev_hdd0/tmp/devilutionx-ps3.log
3. /dev_hdd0/game/DEVILUT00/devilutionx-ps3.log
4. /dev_usb000/devilutionx-ps3.log through /dev_usb005/devilutionx-ps3.log

It uses the first writable location. Connect a USB drive before launching to
allow a root-level log when the HDD targets cannot be opened. A missing log
means the process did not reach the constructor or none of the local filesystem
paths could be opened.

## Current porting status

This is an experimental build-and-boot port, not a claim of verified in-game
compatibility. Graphics, DS3 input, sound, save/load behavior, VDEC video
playback and long-session stability still need testing on RPCS3 and real PS3
hardware.
