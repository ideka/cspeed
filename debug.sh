#!/usr/bin/env bash
# Starts the game with the debug tools on (CSPEED_DEBUG=1; keys in src/game/debug_tools.cpp), and with a
# track number goes straight into a race on that track (0-15, locked ones too).
# usage: ./debug.sh [track] [build dir, default build-dbg]
# Tracks:
#    0  Monterey
#    1  Silicon Valley
#    2  Highway 1
#    3  San Francisco
#    4  Central Valley
#    5  Santa Cruz
#    6  San Diego
#    7  Clover (N64 only)
#    8  Los Angeles
#    9  Shasta
#   10  Yosemite
#   11  Mojave
#   12  Fuji (N64 only)
#   13  Oval (N64 only)
#   14  California (N64 only)
#   15  San Andreas (N64 only)
set -e
cd "$(dirname "$0")/${2:-build-dbg}"
if [ -z "$1" ]; then
    CSPEED_DEBUG=1 ./CaliforniaSpeedRecompiled.exe &
else
    CSPEED_DEBUG=1 CSPEED_TRACK="$1" ./CaliforniaSpeedRecompiled.exe &
fi
