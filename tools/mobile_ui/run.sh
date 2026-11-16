#!/bin/bash
# Start the game on its own virtual display (1688x768, a landscape phone at 2x).
# GEOM=780x1688 and TERMX/TERMY can be overridden for portrait.
pkill -f "userdir $TK_WORK/user/" ; pkill -f "Xvfb :$TK_DISP " ; sleep 0.5
Xvfb :$TK_DISP -screen 0 ${GEOM:-1688x768}x24 >/dev/null 2>&1 &
sleep 1
cd "$TK_GAME" && DISPLAY=:$TK_DISP CATA_TOUCH_SCRIPT=$TK_WORK/touch.txt ./cataclysm-tiles --userdir $TK_WORK/user/ > $TK_WORK/game.log 2>&1 &
echo started
