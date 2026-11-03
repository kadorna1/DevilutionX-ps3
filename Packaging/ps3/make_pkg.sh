#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-ps3}"
PS3DEV="${PS3DEV:-/usr/local/ps3dev}"
PSL1GHT="${PSL1GHT:-$PS3DEV}"
export PS3DEV PSL1GHT
export PATH="$PS3DEV/bin:$PS3DEV/host/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PS3DEV/portlibs/ppu/bin:$PATH"

APPID="DEVILUT00"
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
SFO="$(find_tool sfo.py sfo)" || die "sfo.py / sfo not found"
PKG="$(find_tool pkg.py pkg)" || die "pkg.py / pkg not found"

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
if ! "$SFO" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKGDIR/PARAM.SFO"; then
  if [[ "$SFO" == *sfo.py ]] && command -v python3 >/dev/null 2>&1; then
    python3 "$SFO" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKGDIR/PARAM.SFO" ||
      die "SFO generation failed"
  else
    die "SFO generation failed"
  fi
fi
[[ -s "$PKGDIR/PARAM.SFO" ]] || die "PARAM.SFO was not created"

OUT_PKG="$OUT_DIR/devilutionx-ps3.pkg"
echo "Creating $OUT_PKG..."
if ! "$PKG" --contentid "$CONTENT_ID" "$PKGDIR/" "$OUT_PKG"; then
  if [[ "$PKG" == *pkg.py ]] && command -v python3 >/dev/null 2>&1; then
    python3 "$PKG" --contentid "$CONTENT_ID" "$PKGDIR/" "$OUT_PKG" ||
      die "PKG generation failed"
  else
    die "PKG generation failed"
  fi
fi
[[ -s "$OUT_PKG" ]] || die "PKG tool did not create $OUT_PKG"

FINALIZER="$(find_tool package_finalize || true)"
if [[ -n "$FINALIZER" ]]; then
  OUT_FINAL="$OUT_DIR/devilutionx-ps3-gnpdrm.pkg"
  cp "$OUT_PKG" "$OUT_FINAL"
  "$FINALIZER" "$OUT_FINAL"
  echo "Created $OUT_FINAL"
fi

ls -lh "$OUT_PKG" ${OUT_FINAL:+"$OUT_FINAL"}
