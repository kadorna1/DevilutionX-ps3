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
PATCH_FILE="$ROOT/Packaging/ps3/patches/sdl2-psl1ght-ppu-flip-wait.patch"
TEXTURE_FREE_PATCH_FILE="$ROOT/Packaging/ps3/patches/sdl2-psl1ght-texture-free-flip-wait.patch"
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

if ! grep -Fq "Wait for the previous flip from the PPU" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  git -C "$SDL2_SOURCE" apply --check "$PATCH_FILE"
  git -C "$SDL2_SOURCE" apply "$PATCH_FILE"
fi

grep -Fq "Wait for the previous flip from the PPU" "$SDL2_SOURCE/$SDL2_RENDERER"
if grep -Fq "gcmSetWaitFlip(data->context)" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  echo "error: SDL2 renderer still queues gcmSetWaitFlip" >&2
  exit 1
fi

# SDL_RenderPresent submits the last flip and returns while it may still be in
# flight. Wait before DestroyTexture releases RSX source memory, independently
# of whether the previous flip patch was already present in the restored cache.
if ! grep -Fq "Wait for pending RSX flip before freeing texture memory" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  git -C "$SDL2_SOURCE" apply --check "$TEXTURE_FREE_PATCH_FILE"
  git -C "$SDL2_SOURCE" apply "$TEXTURE_FREE_PATCH_FILE"
fi
grep -Fq "Wait for pending RSX flip before freeing texture memory" "$SDL2_SOURCE/$SDL2_RENDERER"
if grep -Fq "TODO: Wait for the DMA transfer to complete" "$SDL2_SOURCE/$SDL2_RENDERER"; then
  echo "error: SDL2 texture destruction still has an unsatisfied DMA wait TODO" >&2
  exit 1
fi

# Guard and trace RSX texture allocation. Use an exact function replacement
# instead of git-apply for this hunk: the surrounding SDL renderer receives
# other patches above, and this avoids fragile unified-diff offsets/context.
python3 - "$SDL2_SOURCE/$SDL2_RENDERER" <<'PYTHON'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
source = source_path.read_bytes().decode("utf-8")
start_marker = "\n".join([
    "static int",
    "PSL1GHT_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)",
    "{",
])
end_marker = "\n".join(["", "static int", "PSL1GHT_UpdateTexture"])
start = source.find(start_marker)
end = source.find(end_marker, start + len(start_marker))
if start < 0 or end < 0:
    raise SystemExit("error: could not locate the exact SDL2 PSL1GHT_CreateTexture function")

current = source[start:end]
required = (
    "PSL1GHT texture create begin",
    "PSL1GHT texture allocation failed",
    "SDL_OutOfMemory();",
    "rsxFree(pixels);",
)
if all(marker in current for marker in required):
    print("SDL2 RSX texture allocation guards already present")
else:
    expected = """static int
PSL1GHT_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    int bpp;
    int pitch;
    void *pixels;
    Uint32 Rmask, Gmask, Bmask, Amask;

    if (!SDL_PixelFormatEnumToMasks
        (texture->format, &bpp, &Rmask, &Gmask, &Bmask, &Amask)) {
        SDL_SetError("Unknown texture format");
        return -1;
    }

    // Allocate GFX memory for textures
    pitch = texture->w * SDL_BYTESPERPIXEL(texture->format);
    pixels = rsxMemalign(64, texture->h * pitch);

    texture->driverdata =
        SDL_CreateRGBSurfaceFrom(pixels, texture->w, texture->h, bpp, pitch,
                            Rmask, Gmask, Bmask, Amask);

    SDL_SetSurfaceColorMod(texture->driverdata, texture->r, texture->g,
                           texture->b);
    SDL_SetSurfaceAlphaMod(texture->driverdata, texture->a);
    SDL_SetSurfaceBlendMode(texture->driverdata, texture->blendMode);

    if (!texture->driverdata) {
        return -1;
    }
    return 0;
}"""
    if current != expected:
        raise SystemExit(
            "error: SDL2 PSL1GHT_CreateTexture differs from the verified upstream function; "
            "refusing an unsafe automatic replacement"
        )

    replacement = """static int
PSL1GHT_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    int bpp;
    int pitch;
    void *pixels;
    Uint32 Rmask, Gmask, Bmask, Amask;

    SDL_Log("PSL1GHT texture create begin: %dx%d format=0x%08x",
        texture->w, texture->h, texture->format);
    if (!SDL_PixelFormatEnumToMasks
        (texture->format, &bpp, &Rmask, &Gmask, &Bmask, &Amask)) {
        SDL_SetError("Unknown texture format");
        SDL_Log("PSL1GHT texture create failed: unsupported pixel format");
        return -1;
    }

    // Allocate GFX memory for textures
    pitch = texture->w * SDL_BYTESPERPIXEL(texture->format);
    const size_t allocationSize = (size_t)texture->h * (size_t)pitch;
    SDL_Log("PSL1GHT texture create: RSX allocation begin bytes=%u pitch=%d",
        (unsigned int)allocationSize, pitch);
    pixels = rsxMemalign(64, allocationSize);
    if (!pixels) {
        SDL_Log("PSL1GHT texture allocation failed: rsxMemalign returned NULL");
        SDL_OutOfMemory();
        return -1;
    }
    SDL_Log("PSL1GHT texture create: RSX allocation complete ptr=%p", pixels);

    texture->driverdata =
        SDL_CreateRGBSurfaceFrom(pixels, texture->w, texture->h, bpp, pitch,
                            Rmask, Gmask, Bmask, Amask);
    if (!texture->driverdata) {
        SDL_Log("PSL1GHT texture create failed: SDL_CreateRGBSurfaceFrom: %s",
            SDL_GetError());
        rsxFree(pixels);
        return -1;
    }
    SDL_Log("PSL1GHT texture create: surface complete ptr=%p", texture->driverdata);

    SDL_SetSurfaceColorMod(texture->driverdata, texture->r, texture->g,
                           texture->b);
    SDL_SetSurfaceAlphaMod(texture->driverdata, texture->a);
    SDL_SetSurfaceBlendMode(texture->driverdata, texture->blendMode);

    SDL_Log("PSL1GHT texture create complete: %dx%d bytes=%u",
        texture->w, texture->h, (unsigned int)allocationSize);
    return 0;
}"""
    source = source[:start] + replacement + source[end:]
    source_path.write_bytes(source.encode("utf-8"))
    print("Applied exact SDL2 RSX texture allocation guard replacement")

updated = source_path.read_bytes().decode("utf-8")
function_start = updated.find(start_marker)
function_end = updated.find(end_marker, function_start + len(start_marker))
function = updated[function_start:function_end]
for marker in required:
    if marker not in function:
        raise SystemExit(f"error: SDL2 RSX texture allocation guard missing after edit: {marker}")
print("Validated SDL2 RSX texture allocation and cleanup guards")
PYTHON

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
  echo "Renderer patches: PPU-side flip polling, wait-before-free, and guarded RSX texture allocation"
  echo "PS3 joystick source blob: $actual_joystick_blob"
  echo "Controller patch: initialize pad state and center the four analog axes"
  echo "Controller mapping: DualShock 3 4-axis / 16-button ioPad layout"
  echo "SDL2 version: $(sdl2-config --version)"
  echo "SDL2 prefix: $SDL2_PREFIX"
  echo "SDL2 archive: $SDL2_ARCHIVE"
  echo "SDL2 archive bytes: $(stat -c %s "$SDL2_ARCHIVE")"
} | tee "$BUILD_DIR/sdl2-psl1ght-flip-fix-info.txt"
