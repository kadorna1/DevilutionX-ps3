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
# The linked main application is a normal PS3 executable, not an SPRX module.
# sprxlinker requires the PRX-only .sys_proc_prx_param section, which the
# application ELF intentionally does not contain. make_self_npdrm consumes
# the executable ELF directly and creates the EBOOT.BIN SELF wrapper.
"$SELF" "$WORK/devilutionx.stripped.elf" "$PKGDIR/USRDIR/EBOOT.BIN" "$CONTENT_ID"
[[ -s "$PKGDIR/USRDIR/EBOOT.BIN" ]] || die "make_self_npdrm did not produce EBOOT.BIN"

echo "Verifying NPDRM SELF header..."
python3 - "$PKGDIR/USRDIR/EBOOT.BIN" <<'PY'
from pathlib import Path
import struct
import sys

p = Path(sys.argv[1])
with p.open("rb") as f:
    header = f.read(0x80)
if len(header) < 0x80 or header[:4] != b"SCE\0":
    raise SystemExit("EBOOT.BIN is missing a valid SELF header")
header_version, flags, self_type = struct.unpack(">IHH", header[4:12])
if (header_version, flags, self_type) != (2, 1, 1):
    raise SystemExit(
        f"unexpected SELF fields: version={header_version}, "
        f"flags=0x{flags:04x}, type={self_type}"
    )
app_info_offset = struct.unpack(">Q", header[40:48])[0]
with p.open("rb") as f:
    f.seek(app_info_offset + 12)
    app_type_bytes = f.read(4)
if len(app_type_bytes) != 4:
    raise SystemExit("EBOOT.BIN SELF is missing its AppInfo record")
app_type = struct.unpack(">I", app_type_bytes)[0]
if app_type != 8:
    raise SystemExit(f"EBOOT.BIN is not NPDRM (AppInfo appType={app_type})")
print(f"NPDRM SELF OK: {p.stat().st_size} bytes (appType={app_type})")
PY

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

echo "Adding PS3 XMB media..."
XMB_DIR="$ROOT/assets/xmb"
for asset in ICON0.PNG LOGO0.PNG PIC1.PNG SND0.AT3; do
  [[ -s "$XMB_DIR/$asset" ]] || die "required XMB asset missing: $XMB_DIR/$asset"
  cp "$XMB_DIR/$asset" "$PKGDIR/$asset"
done
python3 - "$PKGDIR" <<'PY'
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
expected = {
    "ICON0.PNG": (320, 176),
    "LOGO0.PNG": (320, 176),
    "PIC1.PNG": (1920, 1080),
}
for name, dimensions in expected.items():
    path = root / name
    with path.open("rb") as image:
        header = image.read(24)
    if len(header) < 24 or header[:8] != bytes([137, 80, 78, 71, 13, 10, 26, 10]):
        raise SystemExit(f"{name} is not a PNG file")
    actual = struct.unpack(">II", header[16:24])
    if actual != dimensions:
        raise SystemExit(f"{name} has dimensions {actual}, expected {dimensions}")
    print(f"XMB image OK: {name} {actual[0]}x{actual[1]}")

sound = root / "SND0.AT3"
if sound.stat().st_size > 2_400_000:
    raise SystemExit(f"SND0.AT3 exceeds the PS3 XMB sound size limit: {sound.stat().st_size}")
print(f"XMB sound OK: SND0.AT3 ({sound.stat().st_size} bytes)")
PY
cmp -s "$XMB_DIR/SND0.AT3" "$PKGDIR/SND0.AT3" || die "SND0.AT3 changed during packaging"

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

echo "Verifying PARAM.SFO..."
python3 - "$PKGDIR/PARAM.SFO" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
if p.read_bytes()[:4] != b"\0PSF":
    raise SystemExit("PARAM.SFO has an invalid PSF header")
print(f"PARAM.SFO OK: {p.stat().st_size} bytes")
PY

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

echo "Verifying PKG headers and content IDs..."
python3 - "$CONTENT_ID" "$OUT_PKG" "$OUT_FINAL" <<'PY'
from pathlib import Path
import sys
expected = sys.argv[1].encode("ascii")
for arg in sys.argv[2:]:
    if not arg:
        continue
    p = Path(arg)
    data = p.read_bytes()
    if len(data) < 0x80 or data[:4] != b"\x7fPKG":
        raise SystemExit(f"invalid PKG header: {p}")
    content_id = data[0x30:0x60].split(b"\0", 1)[0]
    if content_id != expected:
        raise SystemExit(f"wrong PKG content ID in {p}: {content_id!r}")
    print(f"PKG OK: {p.name} ({len(data)} bytes, {content_id.decode()})")
PY

ls -lh "$OUT_PKG" ${OUT_FINAL:+"$OUT_FINAL"}
