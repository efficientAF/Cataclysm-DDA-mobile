#!/bin/bash
# Steam Deck: deploys the newest cdda-mobile*.zip (or .tar.gz) from ~/Downloads into
# the folder this script is in.  Copy it into the folder the game is to run from
# (e.g. ~/Games/CDDA-Mobile) as deploy.sh and run: bash deploy.sh
# Layout it keeps:
#   game/       the current build (replaced on every deploy)
#   game.old/   the build before it
#   userdata/   saves, settings, HUD layout (never touched)
#   play.sh     what to run, and what to add to Steam as a non-Steam game
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOWNLOADS="${1:-$HOME/Downloads}"

ARCHIVE="$(ls -t "$DOWNLOADS"/cdda-mobile*.zip "$DOWNLOADS"/cdda-mobile*.tar.gz 2>/dev/null | head -n 1 || true)"
if [ -z "$ARCHIVE" ]; then
    echo "No cdda-mobile*.zip or cdda-mobile*.tar.gz in $DOWNLOADS"
    exit 1
fi
echo "Deploying $ARCHIVE"

TMP="$(mktemp -d "$HERE/.deploy.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
case "$ARCHIVE" in
    *.zip) unzip -q "$ARCHIVE" -d "$TMP" ;;
    *)     tar -xzf "$ARCHIVE" -C "$TMP" ;;
esac

BIN="$(find "$TMP" -maxdepth 3 -type f -name cataclysm-tiles | head -n 1)"
if [ -z "$BIN" ]; then
    echo "No cataclysm-tiles in the archive; nothing was changed."
    exit 1
fi
NEW="$(dirname "$BIN")"
chmod +x "$BIN"

rm -rf "$HERE/game.old"
[ -d "$HERE/game" ] && mv "$HERE/game" "$HERE/game.old"
mv "$NEW" "$HERE/game"
mkdir -p "$HERE/userdata"

cat > "$HERE/play.sh" <<'PLAY'
#!/bin/bash
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE/game"
export LD_LIBRARY_PATH="$HERE/game/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./cataclysm-tiles --mobile-ui --userdir "$HERE/userdata/" "$@"
PLAY
chmod +x "$HERE/play.sh"

echo "Done. Run $HERE/play.sh (add that file to Steam as a non-Steam game)."
echo "The previous build is in game.old; your saves are in userdata."
