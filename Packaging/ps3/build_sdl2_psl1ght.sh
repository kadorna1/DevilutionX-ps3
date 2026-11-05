#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-ps3}"

if [[ -z "${PS3DEV:-}" ]]; then
  echo "error: PS3DEV must point to the PSL1GHT SDK" >&2
  exit 1
fi
export PSL1GHT="${PSL1GHT:-$PS3DEV}"
export PATH="$PS3DEV/bin:$PS3DEV/host/bin:$PS3DEV/ppu/bin:$PS3DEV/portlibs/ppu/bin:$PATH"

for tool in git make ppu-gcc; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "error: required SDL2 build tool not found: $tool" >&2
    exit 1
  }
done

# Match the SDL2_PSL1GHT source used by the ps3dev-sdl2 portlibs image.
# The renderer blob hash below deliberately guards the patch context: if
# upstream changes this backend, stop rather than silently building an
# unreviewed renderer.
SDL2_REPO="https://github.com/shagkur/SDL_PSL1GHT.git"
SDL2_BRANCH="sdl2_master"
SDL2_RENDERER="src/render/psl1ght/SDL_PSL1GHTrender.c"
SDL2_RENDERER_BLOB="3d5cf9b41bc7639d1da79b6f7059350a1bf57052"
SDL2_JOYSTICK="src/joystick/psl1ght/SDL_sysjoystick.c"
SDL2_JOYSTICK_BLOB="f7b2d2a8239c2052653f95ce155e656e9bdc431e"
SDL2_SOURCE="$BUILD_DIR/_deps/sdl2-psl1ght"
JOYSTICK_PATCH_FILE="$ROOT/Packaging/ps3/patches/sdl2-psl1ght-controller-input.patch"

mkdir -p "$(dirname "$SDL2_SOURCE")"
if [[ ! -d "$SDL2_SOURCE/.git" ]]; then
  rm -rf "$SDL2_SOURCE"
  git clone --depth 1 --single-branch --branch "$SDL2_BRANCH" "$SDL2_REPO" "$SDL2_SOURCE"
fi

actual_blob="$(git -C "$SDL2_SOURCE" rev-parse "HEAD:$SDL2_RENDERER")"
if [[ "$actual_blob" != "$SDL2_RENDERER_BLOB" ]]; then
  echo "error: SDL2 PSL1GHT renderer source changed unexpectedly" >&2
  echo "expected blob: $SDL2_RENDERER_BLOB" >&2
  echo "actual blob:   $actual_blob" >&2
  exit 1
fi

# The restored build cache may contain a working tree left modified by an
# earlier patch attempt. Materialize the exact raw Git blobs rather than using
# checkout/restore filters, so line endings and cached working-tree settings
# cannot make a valid patch appear not to apply. This preserves Makefiles and
# object files while resetting only the two source files patched below.
git -C "$SDL2_SOURCE" show "HEAD:$SDL2_RENDERER" > "$SDL2_SOURCE/$SDL2_RENDERER"
git -C "$SDL2_SOURCE" show "HEAD:$SDL2_JOYSTICK" > "$SDL2_SOURCE/$SDL2_JOYSTICK"

renderer_worktree_blob="$(git -C "$SDL2_SOURCE" hash-object -- "$SDL2_RENDERER")"
joystick_worktree_blob="$(git -C "$SDL2_SOURCE" hash-object -- "$SDL2_JOYSTICK")"
if [[ "$renderer_worktree_blob" != "$SDL2_RENDERER_BLOB" ]]; then
  echo "error: SDL2 renderer working copy does not match its verified Git blob" >&2
  echo "expected blob: $SDL2_RENDERER_BLOB" >&2
  echo "actual blob:   $renderer_worktree_blob" >&2
  exit 1
fi
if [[ "$joystick_worktree_blob" != "$SDL2_JOYSTICK_BLOB" ]]; then
  echo "error: SDL2 joystick working copy does not match its verified Git blob" >&2
  echo "expected blob: $SDL2_JOYSTICK_BLOB" >&2
  echo "actual blob:   $joystick_worktree_blob" >&2
  exit 1
fi
echo "Verified clean raw SDL2 renderer and joystick source blobs"

# Apply reviewed SDL2 backend changes from the exact, blob-verified upstream source.
python3 "$ROOT/Packaging/ps3/patch_sdl2_psl1ght_renderer.py" "$SDL2_SOURCE/$SDL2_RENDERER"
grep -Fq "gcmSetFlipHandler(PSL1GHT_FlipHandler)" "$SDL2_SOURCE/$SDL2_RENDERER"
grep -Fq "gcmSetWaitFlip(data->context)" "$SDL2_SOURCE/$SDL2_RENDERER"
grep -Fq "PSL1GHT_FB_COUNT 3" "$SDL2_SOURCE/$SDL2_RENDERER"
grep -Fq "PSL1GHT_WaitForAllFlips(data)" "$SDL2_SOURCE/$SDL2_RENDERER"
grep -Fq "PSL1GHT_ReserveRSXWords(data->context, 16)" "$SDL2_SOURCE/$SDL2_RENDERER"
grep -Fq "rsxSetWriteBackendLabel(data->context, PSL1GHT_WAIT_IDLE_LABEL, value)" "$SDL2_SOURCE/$SDL2_RENDERER"
if grep -Fq "gcmGetFlipStatus()" "$SDL2_SOURCE/$SDL2_RENDERER" || grep -Fq "gcmResetFlipStatus()" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  echo "error: unreliable RSX flip-status polling remains in SDL2 renderer" >&2
  exit 1
fi
if grep -Fq "waitFlip()" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  echo "error: obsolete SDL2 renderer waitFlip helper remains" >&2
  exit 1
fi

actual_joystick_blob="$(git -C "$SDL2_SOURCE" rev-parse "HEAD:$SDL2_JOYSTICK")"
if [[ "$actual_joystick_blob" != "$SDL2_JOYSTICK_BLOB" ]]; then
  echo "error: SDL2 PSL1GHT joystick source changed unexpectedly" >&2
  echo "expected blob: $SDL2_JOYSTICK_BLOB" >&2
  echo "actual blob:   $actual_joystick_blob" >&2
  exit 1
fi

if ! grep -Fq "Initialize the initial pad snapshot" "$SDL2_SOURCE/$SDL2_JOYSTICK"; then
  git -C "$SDL2_SOURCE" apply --check "$JOYSTICK_PATCH_FILE"
  git -C "$SDL2_SOURCE" apply "$JOYSTICK_PATCH_FILE"
fi
grep -Fq "Initialize the initial pad snapshot" "$SDL2_SOURCE/$SDL2_JOYSTICK"
grep -Fq "(Sint16)(((int)new_pad_data.btn - 0x80) * 256)" "$SDL2_SOURCE/$SDL2_JOYSTICK"

# The first run configures SDL2. Later CI runs restore this source/build tree
# from the existing build cache and only rebuild changed objects.
if [[ ! -f "$SDL2_SOURCE/Makefile" || ! -f "$SDL2_SOURCE/config.status" ]]; then
  (
    cd "$SDL2_SOURCE"
    ./script.sh
  )
fi

make -C "$SDL2_SOURCE" -j"${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"
make -C "$SDL2_SOURCE" install

SDL2_PREFIX="$(sdl2-config --prefix)"
SDL2_ARCHIVE="$SDL2_PREFIX/lib/libSDL2.a"
test -s "$SDL2_ARCHIVE" || {
  echo "error: patched SDL2 static library was not installed at $SDL2_ARCHIVE" >&2
  exit 1
}

{
  echo "SDL2 source: $SDL2_REPO ($SDL2_BRANCH)"
  echo "SDL2 source commit: $(git -C "$SDL2_SOURCE" rev-parse HEAD)"
  echo "SDL2 renderer source blob: $actual_blob"
  echo "Renderer patches: hardware-tested callback-driven triple buffering, rsxkit FIFO reservation and RSX DMA fences, PPC scanout barrier, explicit unsupported paths, and RSX memory accounting"
  echo "PS3 joystick source blob: $actual_joystick_blob"
  echo "Controller patch: initialize pad state and center the four analog axes"
  echo "Controller mapping: DualShock 3 4-axis / 16-button ioPad layout"
  echo "SDL2 version: $(sdl2-config --version)"
  echo "SDL2 prefix: $SDL2_PREFIX"
  echo "SDL2 archive: $SDL2_ARCHIVE"
  echo "SDL2 archive bytes: $(stat -c %s "$SDL2_ARCHIVE")"
} | tee "$BUILD_DIR/sdl2-psl1ght-flip-fix-info.txt"
