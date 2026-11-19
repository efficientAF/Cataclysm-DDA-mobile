#!/bin/bash
# Download a sound pack into data/sound/, where the desktop game, the Steam Deck
# package and the Android build pick it up.  The pack is CC-Sounds, the one the
# game chooses by itself when it is there, from the latest release of
# https://github.com/Fris0uman/CDDA-Soundpacks (about 140 MB to download).
# data/sound/CC-Sounds is ignored by git.
# Usage: fetch_soundpack.sh [name]   (another pack of that project, e.g. CO.AG)
set -u
SOUND=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../data/sound" && pwd)
name=${1:-CC-Sounds}
URL=https://github.com/Fris0uman/CDDA-Soundpacks/releases/latest/download
if [ -f "$SOUND/$name/soundpack.txt" ]; then echo "have $name"; exit 0; fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
if ! curl -fsSL --retry 4 -o "$TMP/s.zip" "$URL/$name.zip"; then
  echo "could not download $name"; exit 1
fi
if ! unzip -q -o "$TMP/s.zip" -d "$TMP/x"; then
  echo "could not unpack $name.zip"; exit 1
fi
# the zip holds the pack's folder, or its files directly
src=$(dirname "$(find "$TMP/x" -name soundpack.txt | head -1)")
if [ ! -f "$src/soundpack.txt" ]; then
  echo "no soundpack.txt in $name.zip"; exit 1
fi
rm -rf "$SOUND/$name" && mv "$src" "$SOUND/$name" && echo "got $name"
