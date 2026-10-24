#!/bin/sh
# Launch the SDL2 dual-screen test from the stock firmware's Apps menu.
# Install: copy this script and the `dualscreen` binary (same folder) to the Roms/APPS folder of the
# game card (the stock firmware lists .sh files there under Applications > Apps).
# Uses the environment the stock launcher gives its own apps (see docs/FINDINGS.md).
cd "$(dirname "$0")" || exit 1
export XDG_RUNTIME_DIR=/var/run
export WAYLAND_DISPLAY=wayland-0
export SDL_VIDEODRIVER=wayland
export LD_LIBRARY_PATH="/usr/lib:/lib"
chmod +x ./dualscreen 2>/dev/null
./dualscreen > /tmp/dualscreen.log 2>&1
# the log is on tmpfs; copy it next to the script so you can read it on a computer
cp /tmp/dualscreen.log ./dualscreen.log 2>/dev/null
