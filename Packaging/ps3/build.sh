#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-ps3}"
BUILD_ONLY=0
if [[ "$BUILD_DIR" == "--build-only" ]]; then
  BUILD_ONLY=1
  BUILD_DIR="${2:-build-ps3}"
elif [[ "${2:-}" == "--build-only" ]]; then
  BUILD_ONLY=1
fi

if [[ -z "${PS3DEV:-}" ]]; then
  if [[ -d /usr/local/ps3dev ]]; then
    export PS3DEV=/usr/local/ps3dev
  else
    echo "error: PS3DEV is not set and /usr/local/ps3dev does not exist" >&2
    exit 1
  fi
fi
export PSL1GHT="${PSL1GHT:-$PS3DEV}"
export PATH="$PS3DEV/bin:$PS3DEV/host/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PS3DEV/portlibs/ppu/bin:$PATH"

for tool in ppu-gcc ppu-g++ sdl2-config cmake ninja; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: required PS3 build tool not found: $tool" >&2
    exit 1
  fi
done

echo "PS3DEV: $PS3DEV"
ppu-g++ --version | head -n 1
echo "SDL2: $(sdl2-config --version)"

SDL2_DIR="$PS3DEV/portlibs/ppu/lib/cmake/SDL2"
cmake_args=(
  -S "$ROOT"
  -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/CMake/ps3-psl1ght.cmake"
  -DPS3=ON
  -DDEVILUTIONX_PS3_TITLE_ID=DEVILUT00
  -DUSE_SDL1=OFF
  -DUSE_SDL3=OFF
  -DDEVILUTIONX_SYSTEM_SDL2=ON
  -DDEVILUTIONX_STATIC_SDL2=ON
  -DDEVILUTIONX_SYSTEM_SDL_IMAGE=OFF
  -DDEVILUTIONX_SYSTEM_SDL_AUDIOLIB=OFF
  -DBUILD_ASSETS_MPQ=OFF
  -DBUILD_MOD_MPQS=OFF
  -DUNPACKED_SAVES=ON
  -DBUILD_TESTING=OFF
  -DNONET=ON
  -DDISCORD_INTEGRATION=OFF
  -DDISABLE_LTO=ON
  -DDEVILUTIONX_STATIC_CXX_STDLIB=ON
)
if [[ -f "$SDL2_DIR/SDL2Config.cmake" ]]; then
  cmake_args+=(-DSDL2_DIR="$SDL2_DIR")
fi

cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"

if [[ "$BUILD_ONLY" -eq 0 ]]; then
  bash "$ROOT/Packaging/ps3/make_pkg.sh" "$BUILD_DIR"
fi
