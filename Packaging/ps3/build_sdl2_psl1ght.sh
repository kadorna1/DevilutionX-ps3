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
SDL2_SOURCE="$BUILD_DIR/_deps/sdl2-psl1ght"
PATCH_FILE="$ROOT/Packaging/ps3/patches/sdl2-psl1ght-ppu-flip-wait.patch"

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

if ! grep -Fq "Wait for the previous flip from the PPU" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  git -C "$SDL2_SOURCE" apply --check "$PATCH_FILE"
  git -C "$SDL2_SOURCE" apply "$PATCH_FILE"
fi

grep -Fq "Wait for the previous flip from the PPU" "$SDL2_SOURCE/$SDL2_RENDERER"
if grep -Fq "gcmSetWaitFlip(data->context)" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  echo "error: SDL2 renderer still queues gcmSetWaitFlip" >&2
  exit 1
fi

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
  echo "Renderer patch: PPU-side flip polling; no gcmSetWaitFlip command"
  echo "SDL2 version: $(sdl2-config --version)"
  echo "SDL2 prefix: $SDL2_PREFIX"
  echo "SDL2 archive: $SDL2_ARCHIVE"
  echo "SDL2 archive bytes: $(stat -c %s "$SDL2_ARCHIVE")"
} | tee "$BUILD_DIR/sdl2-psl1ght-flip-fix-info.txt"
