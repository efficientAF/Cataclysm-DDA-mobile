#!/bin/bash
# Builds the Steam Deck package: an optimised Linux build with the SDL3 libraries
# beside it, as cdda-mobile-<date>.zip in the repository folder (git ignores it
# through .git/info/exclude: add "deck-*" and "cdda-mobile-*" there).
# Run from the repository after setup_env.sh and fetch_tilesets.sh.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
make -j4 TILES=1 SOUND=0 LOCALIZE=0 CCACHE=1 RELEASE=1 LTO=0 RUNTESTS=0 ASTYLE=0 LINTJSON=0 \
    DEBUGSYMS= BUILD_PREFIX=deck- deck-cataclysm-tiles
OUT="$(mktemp -d)"
P="$OUT/cdda-mobile"
mkdir -p "$P/lib"
cp deck-cataclysm-tiles "$P/cataclysm-tiles"
strip "$P/cataclysm-tiles"
cp -r data gfx LICENSE.txt "$P/"
cp -L /usr/local/lib/libSDL3.so.0 /usr/local/lib/libSDL3_image.so.0 /usr/local/lib/libSDL3_ttf.so.0 "$P/lib/"
git rev-parse --short HEAD > "$P/BUILD.txt"
cp tools/mobile_ui/deck_deploy.sh "$OUT/deploy.sh"
ZIP="$PWD/cdda-mobile-$(date +%F).zip"
rm -f "$ZIP"
( cd "$OUT" && zip -qr "$ZIP" cdda-mobile deploy.sh )
rm -rf "$OUT"
echo "$ZIP"
