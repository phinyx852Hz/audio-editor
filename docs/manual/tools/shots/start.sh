#!/bin/bash
# Starts Xvfb + openbox + PHI with a clean home. Usage: start.sh [file]
export DISPLAY=:99
export HOME=/home/user/shothome
export XDG_RUNTIME_DIR=/tmp/xdg-shots; mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
mkdir -p $HOME
pgrep -f "Xvfb :99" >/dev/null || setsid nohup Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp >/tmp/xvfb.log 2>&1 </dev/null &
sleep 1
pgrep -x openbox >/dev/null || setsid nohup openbox >/tmp/openbox.log 2>&1 </dev/null &
sleep 1
export QT_QPA_PLATFORM=xcb
pkill -x phi; sleep 0.5
setsid nohup /home/user/build/phi_artefacts/Release/phi "$@" >/tmp/phi.log 2>&1 </dev/null &
sleep 4
