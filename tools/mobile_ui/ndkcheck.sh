#!/bin/bash
# Syntax-check worktree source files with the NDK compile flags from the last Gradle build.
S=${NDK_CMD_DIR:-/tmp}   # holds ndk_cmd.txt: the clang++ command line for sdltiles.cpp from android/app/.cxx/**/compile_commands.json, then its directory
CMD=$(head -1 $S/ndk_cmd.txt | sed 's| -o [^ ]*sdltiles.cpp.o| -fsyntax-only|; s| -c /home/user/Cataclysm-DDA-mobile/src/sdltiles.cpp||; s|/home/user/Cataclysm-DDA-mobile/src|/home/user/Cataclysm-DDA-mobile/src|g; s| -MD -MT [^ ]* -MF [^ ]*||; s|-Os|-O0|g')
rc=0
for f in "$@"; do
  echo "== $f"; (cd /home/user/Cataclysm-DDA-mobile && $CMD -c /home/user/Cataclysm-DDA-mobile/src/$f 2>&1 | grep -E "error|warning: unused" -A4 | head -40); 
done
