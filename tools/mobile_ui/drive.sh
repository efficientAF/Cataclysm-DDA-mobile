#!/bin/bash
# drive.sh wait | keys K1 K2.. | tap X Y | touch FILE | mouse X Y | shot NAME
#  tap X Y: X,Y in window pixels; parks the real mouse there first (needed on X11 for ImGui), then sends a scripted finger tap
export DISPLAY=:$TK_DISP
cmd=$1; shift
size() { xdpyinfo | awk '/dimensions/{print $2}'; }
case $cmd in
  wait) for i in $(seq 1 90); do import -window root $TK_WORK/shots/w.png; if python3 -c "from PIL import Image; import sys; sys.exit(0 if Image.open('$TK_WORK/shots/w.png').getbbox() else 1)"; then echo "ready after $i"; exit 0; fi; sleep 2; done; echo "still blank";;
  keys) for k in "$@"; do xdotool key "$k"; sleep 0.4; done;;
  mouse) xdotool mousemove $1 $2;;
  tap) WH=$(size); W=${WH%x*}; H=${WH#*x}; xdotool mousemove $1 $2; sleep 0.3
       python3 -c "print('0 down 0 %.4f %.4f\n80 up 0 %.4f %.4f' % ($1/$W,$2/$H,$1/$W,$2/$H))" > $TK_WORK/touch.tmp && mv $TK_WORK/touch.tmp $TK_WORK/touch.txt; sleep 1.6;;
  touch) cp "$1" $TK_WORK/touch.tmp && mv $TK_WORK/touch.tmp $TK_WORK/touch.txt; last=$(tail -1 "$1" | cut -d' ' -f1); sleep $(python3 -c "print(($last+1500)/1000)");;
  shot) import -window root $TK_WORK/shots/$1.png && convert $TK_WORK/shots/$1.png -scale 50% $TK_WORK/shots/$1-half.png && echo $TK_WORK/shots/$1-half.png;;
esac
