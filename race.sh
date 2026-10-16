#!/usr/bin/env bash
# Opens the game straight into a race on the given track (0-15) with the debug tools on.
# usage: ./race.sh <track> [build dir, default build-dbg]
set -e
if [ -z "$1" ]; then
    echo "usage: $0 <track 0-15> [build dir]" >&2
    exit 1
fi
cd "$(dirname "$0")/${2:-build-dbg}"
CSPEED_DEBUG=1 CSPEED_TRACK="$1" ./CaliforniaSpeedRecompiled.exe &
