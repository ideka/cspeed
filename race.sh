#!/usr/bin/env bash
# Opens the game straight into a race on the given track (0-15) with the debug tools on.
# usage: ./race.sh <track> [build dir, default build-dbg]
# Tracks: 0 Monterey, 1 Silicon Valley, 2 Highway 1, 3 San Francisco, 4 Central Valley, 5 Santa Cruz,
#   6 San Diego, 7 Clover, 8 Los Angeles, 9 Shasta, 10 Yosemite, 11 Mojave, 12 Fuji, 13 Oval, 14 California,
#   15 San Andreas (7 and 12-15 are N64 only).
set -e
if [ -z "$1" ]; then
    echo "usage: $0 <track 0-15> [build dir]" >&2
    exit 1
fi
cd "$(dirname "$0")/${2:-build-dbg}"
CSPEED_DEBUG=1 CSPEED_TRACK="$1" ./CaliforniaSpeedRecompiled.exe &
