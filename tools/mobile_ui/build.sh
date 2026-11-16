#!/bin/bash
# Build the desktop game in $TK_GAME. Prints EXIT code and only real errors.
cd "$TK_GAME" && make -j2 TILES=1 SOUND=0 LOCALIZE=0 CCACHE=0 RUNTESTS=0 ASTYLE=0 LINTJSON=0 DEBUGSYMS= cataclysm-tiles > $TK_WORK/build.log 2>&1
echo "EXIT $?"; grep -n " error: " -A4 $TK_WORK/build.log | cut -c1-300 | head -60
