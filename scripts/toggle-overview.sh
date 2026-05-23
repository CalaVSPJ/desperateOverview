#!/usr/bin/env bash
CX=$(hyprctl -j cursorpos 2>/dev/null | grep -o '"x": *[0-9-]*' | grep -o '[0-9-]*$')
[ "${CX:-0}" -lt 1440 ] && exit 0
if pgrep -f "desperateOverview --show" >/dev/null; then
    pkill -f "desperateOverview --show"
else
    desperateOverview --show
fi
