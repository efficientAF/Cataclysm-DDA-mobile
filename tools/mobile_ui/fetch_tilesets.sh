#!/bin/bash
# Download the picture tilesets into gfx/, where the desktop game and the Android
# build pick them up.  They come ready-made from the latest release of the
# tileset project (https://github.com/I-am-Erk/CDDA-Tilesets, CC BY-SA), the same
# source upstream's release builds compose them from.  gfx/* is ignored by git.
# Usage: fetch_tilesets.sh [name ...]   (no names: all of them)
set -u
GFX=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../gfx" && pwd)
ALL="UltimateCataclysm MShockXotto+ MShockXotto+_Portraits Altica ChibiUltica BrownLikeBears \
HollowMoon GiantDays PenAndPaper SurveyorsMap ASCII_Overmap Ultica_iso SmashButton_iso"
URL=https://github.com/I-am-Erk/CDDA-Tilesets/releases/latest/download
TMP=$(mktemp -d)
failed=0
for name in ${@:-$ALL}; do
  if [ -f "$GFX/$name/tileset.txt" ]; then echo "have $name"; continue; fi
  if curl -fsSL --retry 4 -o "$TMP/t.zip" "$URL/${name//+/%2B}.zip" && unzip -q -o "$TMP/t.zip" -d "$TMP/x"; then
    # the zip holds the tileset's folder, or its files directly
    src=$(dirname "$(find "$TMP/x" -name tileset.txt | head -1)")
    if [ -f "$src/tileset.txt" ]; then
      rm -rf "$GFX/$name" && mv "$src" "$GFX/$name" && echo "got $name"
    else
      echo "no tileset.txt in $name.zip"; failed=1
    fi
  else
    echo "could not download $name"; failed=1
  fi
  rm -rf "$TMP/x" "$TMP/t.zip"
done
rm -rf "$TMP"
exit $failed
