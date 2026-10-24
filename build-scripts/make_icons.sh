#!/usr/bin/env bash
# Regenerates the app icons (logo.ico, JPet.icns) from resources/icon/logo.svg
# and the tray icons (tray-*.ico, tray*.png) from resources/icon/tray.svg.
# Requires macOS (iconutil), rsvg-convert (brew install librsvg) and python3.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT_DIR}/resources/icon/logo.svg"
TRAY_SRC="${ROOT_DIR}/resources/icon/tray.svg"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

for tool in rsvg-convert iconutil python3; do
  command -v "${tool}" >/dev/null || { echo "Missing ${tool}" >&2; exit 1; }
done

# The full artwork turns to noise below 48px, so small sizes use a crop of the head.
python3 - "${SRC}" "${WORK}/small.svg" <<'EOF'
import re, sys
svg = open(sys.argv[1], encoding="utf-8").read()
x, y, size, radius = 330, 170, 740, 170
svg = re.sub(r'viewBox="[^"]*"', f'viewBox="{x} {y} {size} {size}"', svg, count=1)
svg = re.sub(r'<rect [^>]*/></clipPath>',
             f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="{radius}"/></clipPath>', svg, count=1)
open(sys.argv[2], "w", encoding="utf-8").write(svg)
EOF

render() { # svg size out
  rsvg-convert -w "$2" -h "$2" "$1" -o "$3"
}

art_for() { # size -> svg
  if (( $1 < 48 )); then echo "${WORK}/small.svg"; else echo "${SRC}"; fi
}

write_ico() { # out png-prefix sizes...
  python3 - "$@" <<'EOF'
import struct, sys
out, prefix, sizes = sys.argv[1], sys.argv[2], [int(s) for s in sys.argv[3:]]
images = [open(f"{prefix}{s}.png", "rb").read() for s in sizes]
header = struct.pack("<HHH", 0, 1, len(images))
offset = 6 + 16 * len(images)
entries = b""
for size, data in zip(sizes, images):
    entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
open(out, "wb").write(header + entries + b"".join(images))
EOF
}

# Windows: PNG-compressed ICO entries.
ICO_SIZES=(16 24 32 48 64 128 256)
for size in "${ICO_SIZES[@]}"; do
  render "$(art_for "${size}")" "${size}" "${WORK}/ico-${size}.png"
done
write_ico "${ROOT_DIR}/resources/logo.ico" "${WORK}/ico-" "${ICO_SIZES[@]}"

# Windows tray: no automatic tinting, so ship a dark glyph for light taskbars and a white one for dark.
sed 's/<path /<path fill="#FFFFFF" /' "${TRAY_SRC}" > "${WORK}/tray-white.svg"
TRAY_SIZES=(16 20 24 32 40 48)
for size in "${TRAY_SIZES[@]}"; do
  render "${TRAY_SRC}" "${size}" "${WORK}/tray-light-${size}.png"
  render "${WORK}/tray-white.svg" "${size}" "${WORK}/tray-dark-${size}.png"
done
write_ico "${ROOT_DIR}/resources/tray-light.ico" "${WORK}/tray-light-" "${TRAY_SIZES[@]}"
write_ico "${ROOT_DIR}/resources/tray-dark.ico" "${WORK}/tray-dark-" "${TRAY_SIZES[@]}"

# macOS menu bar: template image, tinted by the system.
render "${TRAY_SRC}" 18 "${ROOT_DIR}/resources/tray.png"
render "${TRAY_SRC}" 36 "${ROOT_DIR}/resources/tray@2x.png"

# macOS: tile on Apple's 1024 grid (824px body, 100px margin) with a soft shadow.
ICONSET="${WORK}/JPet.iconset"
mkdir -p "${ICONSET}"
mac_icon() { # canvas out
  local canvas=$1 out=$2 tile pad
  tile=$(( canvas * 824 / 1024 ))
  pad=$(( (canvas - tile) / 2 ))
  render "$(art_for "${tile}")" "${tile}" "${WORK}/tile-${canvas}.png"
  cat > "${WORK}/mac-${canvas}.svg" <<SVG
<svg xmlns="http://www.w3.org/2000/svg" width="${canvas}" height="${canvas}" viewBox="0 0 1024 1024">
  <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">
    <feGaussianBlur in="SourceAlpha" stdDeviation="10"/>
    <feOffset dy="10"/>
    <feComponentTransfer><feFuncA type="linear" slope="0.3"/></feComponentTransfer>
    <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
  <image href="tile-${canvas}.png" x="100" y="100" width="824" height="824" filter="url(#shadow)"/>
</svg>
SVG
  (cd "${WORK}" && rsvg-convert "mac-${canvas}.svg" -o "${out}")
}
for size in 16 32 128 256 512; do
  mac_icon "${size}" "${ICONSET}/icon_${size}x${size}.png"
  mac_icon $(( size * 2 )) "${ICONSET}/icon_${size}x${size}@2x.png"
done
iconutil -c icns "${ICONSET}" -o "${ROOT_DIR}/resources/JPet.icns"

echo "Wrote resources/logo.ico, JPet.icns, tray-light.ico, tray-dark.ico, tray.png and tray@2x.png"
