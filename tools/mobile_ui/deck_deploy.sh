#!/bin/bash
# Steam Deck: deploys the newest cdda-mobile*.zip (or .tar.gz) from ~/Downloads into
# the folder this script is in.  Copy it into the folder the game is to run from
# (e.g. ~/Games/CDDA-Mobile) as deploy.sh and run: bash deploy.sh
# Layout it keeps:
#   game/       the current build (replaced on every deploy)
#   game.old/   the build before it
#   userdata/   saves, settings, HUD layout (never touched)
#   backups/    copies of userdata/config from the last five deploys
#   play.sh     what to run
# The script replaces itself with the one in the zip when that one is newer.
# It also adds a "CDDA Mobile" entry to the application menu, which is what
# Steam's "Add a Non-Steam Game" list shows.
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

# Settings (options, HUD layout, screen sizes, key bindings) are in
# userdata/config and are never touched by a deploy.  A copy is kept anyway,
# of the last five deploys, in case a new build writes something unwanted.
if [ -d "$HERE/userdata/config" ]; then
    mkdir -p "$HERE/backups"
    cp -a "$HERE/userdata/config" "$HERE/backups/config-$(date +%Y%m%d-%H%M%S)"
    ls -1d "$HERE"/backups/config-* | sort | head -n -5 | xargs -r rm -rf
fi

cat > "$HERE/play.sh" <<'PLAY'
#!/bin/bash
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE/game"
export LD_LIBRARY_PATH="$HERE/game/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./cataclysm-tiles --mobile-ui --userdir "$HERE/userdata/" "$@"
PLAY
chmod +x "$HERE/play.sh"

# Steam's "Add a Non-Steam Game" lists installed applications, not scripts:
# a desktop entry makes the game one of them ("CDDA Mobile").
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS"
cat > "$APPS/cdda-mobile.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=CDDA Mobile
Comment=Cataclysm: Dark Days Ahead, touch UI
Exec="$HERE/play.sh"
Path=$HERE
Terminal=false
Categories=Game;
DESKTOP
chmod +x "$APPS/cdda-mobile.desktop"
update-desktop-database "$APPS" 2>/dev/null || true

# The zip carries the deploy script it was made with: take it over if it differs.
# The file is swapped in one step, and this is the last thing the script does.
SELF="$HERE/$(basename "${BASH_SOURCE[0]}")"
if [ -f "$TMP/deploy.sh" ] && ! cmp -s "$TMP/deploy.sh" "$SELF"; then
    cp "$TMP/deploy.sh" "$SELF.new" && mv "$SELF.new" "$SELF" && echo "deploy.sh was updated from the zip."
fi

echo "Done. Run $HERE/play.sh, or start \"CDDA Mobile\" from the application menu."
echo "In Steam: Add a Non-Steam Game, tick \"CDDA Mobile\" in the list."
echo "The previous build is in game.old; your saves are in userdata."
