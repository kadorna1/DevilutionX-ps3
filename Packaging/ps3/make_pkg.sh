#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-ps3}"
PS3DEV="${PS3DEV:-/usr/local/ps3dev}"
PSL1GHT="${PSL1GHT:-$PS3DEV}"
export PS3DEV PSL1GHT
export PATH="$PS3DEV/bin:$PS3DEV/host/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PS3DEV/portlibs/ppu/bin:$PATH"

APPID="DEVILUT00"
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  CACHE_APPID="$(sed -n 's/^DEVILUTIONX_PS3_TITLE_ID:STRING=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)"
  APPID="${CACHE_APPID:-$APPID}"
fi
[[ ${#APPID} -eq 9 && "$APPID" =~ ^[A-Z0-9]+$ ]] || {
  echo "error: invalid PS3 TITLE_ID: $APPID" >&2
  exit 1
}
TITLE="DevilutionX"
CONTENT_ID="UP0001-${APPID}_00-0000000000000000"
ELF="$BUILD_DIR/devilutionx"
SFO_XML="$PS3DEV/bin/sfo.xml"

die() {
  echo "error: $*" >&2
  exit 1
}

find_tool() {
  local name dir
  for name in "$@"; do
    if command -v "$name" >/dev/null 2>&1; then
      command -v "$name"
      return 0
    fi
    for dir in "$PS3DEV/bin" "$PS3DEV/host/bin" "$PS3DEV/ppu/bin" "$PSL1GHT/bin"; do
      if [[ -x "$dir/$name" ]]; then
        echo "$dir/$name"
        return 0
      fi
    done
  done
  return 1
}

[[ -s "$ELF" ]] || die "linked ELF not found: $ELF (run Packaging/ps3/build.sh first)"
[[ -f "$SFO_XML" ]] || die "PS3 SFO template not found: $SFO_XML"

STRIP="$(find_tool ppu-strip)" || die "ppu-strip not found"
SPRX="$(find_tool sprxlinker)" || die "sprxlinker not found"
SELF="$(find_tool make_self_npdrm)" || die "make_self_npdrm not found"
# Prefer the bundled Python helpers, but retry alternatives if their
# runtime/dependencies are unavailable in the builder image.
SFO_CANDIDATES=()
for candidate in sfo.py sfo; do
  path="$(find_tool "$candidate" || true)"
  [[ -n "$path" ]] && SFO_CANDIDATES+=("$path")
done
PKG_CANDIDATES=()
for candidate in pkg.py pkg; do
  path="$(find_tool "$candidate" || true)"
  [[ -n "$path" ]] && PKG_CANDIDATES+=("$path")
done
[[ ${#SFO_CANDIDATES[@]} -gt 0 ]] || die "sfo.py / sfo not found"
[[ ${#PKG_CANDIDATES[@]} -gt 0 ]] || die "pkg.py / pkg not found"

OUT_DIR="$BUILD_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/devilutionx-ps3-pkg.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
PKGDIR="$WORK/pkg"
mkdir -p "$PKGDIR/USRDIR" "$OUT_DIR"

echo "Preparing PS3 SELF..."
"$STRIP" --strip-all "$ELF" -o "$WORK/devilutionx.stripped.elf"
"$SPRX" "$WORK/devilutionx.stripped.elf"
"$SELF" "$WORK/devilutionx.stripped.elf" "$PKGDIR/USRDIR/EBOOT.BIN" "$CONTENT_ID"
[[ -s "$PKGDIR/USRDIR/EBOOT.BIN" ]] || die "make_self_npdrm did not produce EBOOT.BIN"

# Ship DevilutionX's own redistributable engine assets, but never original
# Diablo/Hellfire game data.
if [[ -d "$BUILD_DIR/assets" ]]; then
  mkdir -p "$PKGDIR/USRDIR/assets"
  cp -a "$BUILD_DIR/assets/." "$PKGDIR/USRDIR/assets/"
else
  die "built-in assets directory missing: $BUILD_DIR/assets"
fi
if [[ -s "$BUILD_DIR/devilutionx.mpq" ]]; then
  cp "$BUILD_DIR/devilutionx.mpq" "$PKGDIR/USRDIR/"
fi
if [[ -s "$PS3DEV/bin/ICON0.PNG" ]]; then
  cp "$PS3DEV/bin/ICON0.PNG" "$PKGDIR/ICON0.PNG"
fi

echo "Creating PARAM.SFO..."
sfo_ok=0
for candidate in "${SFO_CANDIDATES[@]}"; do
  rm -f "$PKGDIR/PARAM.SFO"
  if "$candidate" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKGDIR/PARAM.SFO" && [[ -s "$PKGDIR/PARAM.SFO" ]]; then
    sfo_ok=1
    break
  fi
  if [[ "$candidate" == *.py ]]; then
    for python in python2 python3; do
      if command -v "$python" >/dev/null 2>&1 && "$python" "$candidate" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKGDIR/PARAM.SFO" && [[ -s "$PKGDIR/PARAM.SFO" ]]; then
        sfo_ok=1
        break 2
      fi
    done
  fi
done
[[ "$sfo_ok" -eq 1 ]] || die "no SFO tool could create PARAM.SFO"

OUT_PKG="$OUT_DIR/devilutionx-ps3.pkg"
echo "Creating $OUT_PKG..."
pkg_ok=0
for candidate in "${PKG_CANDIDATES[@]}"; do
  rm -f "$OUT_PKG"
  if "$candidate" --contentid "$CONTENT_ID" "$PKGDIR/" "$OUT_PKG" && [[ -s "$OUT_PKG" ]]; then
    pkg_ok=1
    break
  fi
  if [[ "$candidate" == *.py ]]; then
    for python in python2 python3; do
      if command -v "$python" >/dev/null 2>&1 && "$python" "$candidate" --contentid "$CONTENT_ID" "$PKGDIR/" "$OUT_PKG" && [[ -s "$OUT_PKG" ]]; then
        pkg_ok=1
        break 2
      fi
    done
  fi
done
[[ "$pkg_ok" -eq 1 ]] || die "no PKG tool could create $OUT_PKG"

OUT_FINAL=""
FINALIZER="$(find_tool package_finalize || true)"
if [[ -n "$FINALIZER" ]]; then
  OUT_FINAL="$OUT_DIR/devilutionx-ps3-gnpdrm.pkg"
  cp "$OUT_PKG" "$OUT_FINAL"
  "$FINALIZER" "$OUT_FINAL"
  echo "Created $OUT_FINAL"
fi

ls -lh "$OUT_PKG" ${OUT_FINAL:+"$OUT_FINAL"}
