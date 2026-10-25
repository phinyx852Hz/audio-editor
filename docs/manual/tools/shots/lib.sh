# Helpers for driving PHI on :99. Source this file.
export DISPLAY=:99 HOME=/home/user/shothome XDG_RUNTIME_DIR=/tmp/xdg-shots QT_QPA_PLATFORM=xcb
OUTDIR=${OUTDIR:-/home/user/audio-editor/docs/manual/images}
mainwin() { xdotool search --onlyvisible --name ' - PHI$' | head -1; }
win_named() { xdotool search --onlyvisible --name "$1" | tail -1; }
key() { xdotool key --clearmodifiers --delay 60 "$@"; sleep ${KEYWAIT:-0.6}; }
typetext() { xdotool type --delay 40 "$1"; sleep 0.4; }
fit() { local w=$(mainwin); xdotool windowactivate --sync $w 2>/dev/null; xdotool windowmove $w 0 0; xdotool windowsize $w ${1:-1600} ${2:-1000}; sleep 1; }
focusmain() { xdotool windowactivate --sync $(mainwin) 2>/dev/null; sleep 0.3; }
shot() { # shot name [window-id]
  local w=${2:-$(mainwin)}; sleep ${SHOTWAIT:-0.8}
  import -window "$w" "$OUTDIR/$1.png" && echo "shot $1 ($(identify -format '%wx%h' "$OUTDIR/$1.png"))"; }
shotroot() { sleep 0.8; import -window root "$OUTDIR/$1.png"; echo "shot $1"; }
click() { xdotool mousemove --sync $1 $2 click ${3:-1}; sleep 0.5; }
# run a command by its palette name
cmd() { focusmain; key ctrl+shift+p; typetext "$1"; key Return; sleep ${CMDWAIT:-1.5}; }
# newest visible window that is not the main one
topwin() { xdotool search --onlyvisible --name . | while read w; do n=$(xdotool getwindowname $w); [[ "$n" == *" - PHI" || -z "$n" ]] || echo $w; done | tail -1; }
wins() { xdotool search --onlyvisible --name . getwindowname %@; }
# main window area with any popups over it
shotmain() { sleep ${SHOTWAIT:-0.8}; import -window root -crop ${2:-1600x1000+0+0} +repage "$OUTDIR/$1.png"; echo "shot $1"; }
shotwin() { local w=$(topwin); sleep ${SHOTWAIT:-0.8}; import -window "$w" "$OUTDIR/$1.png"; echo "shot $1 [$(xdotool getwindowname $w)] $(identify -format '%wx%h' $OUTDIR/$1.png)"; }
mainwin() { xdotool search --onlyvisible --name '(wav|phi|PHI) - PHI$' | sort -n | head -1; }
topwin() { local m=$(mainwin); for w in $(xdotool search --onlyvisible --name .); do [ "$w" = "$m" ] && continue; [ -n "$(xdotool getwindowname $w)" ] && echo $w; done | tail -1; }
geom() { xdotool getwindowgeometry --shell $1 | awk -F= '/^X=/{x=$2}/^Y=/{y=$2}/^WIDTH=/{w=$2}/^HEIGHT=/{h=$2}END{printf "%dx%d+%d+%d", w,h,x,y+(ENVIRON["GY"]==""?-20:ENVIRON["GY"])}'; }
shotwin() { local w=$(topwin); [ -z "$w" ] && { echo "no window for $1"; return; }; xdotool windowactivate $w 2>/dev/null; sleep ${SHOTWAIT:-0.8}; local g=$(geom $w); timeout 10 import -window root -crop $g +repage "$OUTDIR/$1.png"; echo "shot $1 [$(xdotool getwindowname $w)] $g"; }
shotwin() { local w=$(topwin); [ -z "$w" ] && { echo "no window for $1"; return; }; xdotool windowactivate $w 2>/dev/null; xdotool windowmove $w ${WX:-160} ${WY:-60}; sleep ${SHOTWAIT:-1}; local g=$(geom $w); timeout 10 import -window root -crop $g +repage "$OUTDIR/$1.png"; echo "shot $1 [$(xdotool getwindowname $w)] $g"; }
closetop() { local w=$(topwin); [ -n "$w" ] && xdotool windowclose $w 2>/dev/null; sleep 0.6; }
shotwin() { local w=$(topwin); [ -z "$w" ] && { echo "no window for $1"; return; }; xdotool windowactivate $w 2>/dev/null; xdotool windowmove $w ${WX:-160} ${WY:-60}; [ -n "$WS" ] && xdotool windowsize $w $WS; sleep ${SHOTWAIT:-1.2}; local g=$(geom $w); timeout 10 import -window root -crop $g +repage "$OUTDIR/$1.png"; echo "shot $1 [$(xdotool getwindowname $w)] $g"; }
closetop() { local w=$(topwin); [ -n "$w" ] && { xdotool windowactivate --sync $w 2>/dev/null; xdotool key --clearmodifiers alt+F4; }; sleep 0.8; }
restart() { bash /home/user/shots/start.sh "$@"; sleep 2; for i in 1 2 3; do w=$(topwin); [ -n "$w" ] && [[ "$(xdotool getwindowname $w)" == Recover* ]] && { xdotool windowactivate --sync $w; xdotool key alt+d; sleep 1; }; done; fit; }
mainwin() { xdotool search --onlyvisible --name 'PHI$' | sort -n | head -1; }
play() { focusmain; key ctrl+shift+p; typetext "Play / Stop"; key Return; sleep ${1:-7}; }
stopplay() { focusmain; key ctrl+shift+p; typetext "Stop"; sleep 0.3; xdotool key Return; sleep 0.5; }
