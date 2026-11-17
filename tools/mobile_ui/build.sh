#!/bin/bash
# Build the desktop game in $TK_GAME. Prints EXIT code and only real errors.
# stale.py first: make misses header changes while the precompiled header is on.
cd "$TK_GAME" && python3 tools/mobile_ui/stale.py > /dev/null 2>&1
make -j${TK_JOBS:-2} TILES=1 SOUND=0 LOCALIZE=0 CCACHE=${TK_CCACHE:-0} RUNTESTS=0 ASTYLE=0 LINTJSON=0 DEBUGSYMS= cataclysm-tiles > $TK_WORK/build.log 2>&1
echo "EXIT $?"; grep -n " error: " -A4 $TK_WORK/build.log | cut -c1-300 | head -60
