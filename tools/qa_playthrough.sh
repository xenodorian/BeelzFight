#!/usr/bin/env bash
#
# qa_playthrough.sh -- boot the built disc in headless Flycast, drive it with
# synthetic keyboard input, and screenshot each beat of the level so the
# graphics can be checked in the actual emulator rather than in a mock-up.
#
# Usage: tools/qa_playthrough.sh [disc-image]
# Output: build/qa_play/NN_label.png
#
# Input timing is deliberate: Flycast drops key events sent before its window
# is focused and mapped, so every press is a focused keydown/keyup pair with a
# hold in between rather than a bare `xdotool key`.
set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISC="${1:-$REPO_ROOT/build/bf_small.cdi}"
OUT="$REPO_ROOT/build/qa_play"
DISP=":99"
FLYCAST="${FLYCAST_BIN:-/usr/local/bin/flycast}"

rm -rf "$OUT"; mkdir -p "$OUT"
# -x (exact process name), never -f: a -f pattern also matches the
# shell invoking this script when the script text is on its command line
pkill -x Xvfb 2>/dev/null; pkill -x flycast 2>/dev/null; sleep 1

Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2
export DISPLAY="$DISP"

"$FLYCAST" "$DISC" >"$OUT/flycast.log" 2>&1 &
FLY_PID=$!

cleanup() { kill "$FLY_PID" 2>/dev/null; kill "$XVFB_PID" 2>/dev/null; }
trap cleanup EXIT

sleep 14
WIN=$(xdotool search --name "Flycast" 2>/dev/null | head -1)
[ -n "$WIN" ] && xdotool windowfocus --sync "$WIN" 2>/dev/null

shot() { import -window root "$OUT/$1.png" 2>/dev/null && echo "shot $1"; }
tap()  { xdotool keydown "$1"; sleep "${2:-0.25}"; xdotool keyup "$1"; }
hold() { xdotool keydown "$1"; sleep "$2"; xdotool keyup "$1"; }

shot 01_title
tap Return 0.4; sleep 1.5
shot 02_level_start

# walk right into wave 1, then swing
hold Right 3.0; shot 03_travel
hold Right 2.5; shot 04_wave1
tap a 0.2; sleep 0.2; shot 05_attack_a
tap s 0.2; sleep 0.2; shot 06_attack_b
tap x 0.2; sleep 0.2; shot 07_attack_x
tap q 0.4; shot 08_parry
xdotool keydown w; sleep 0.6; shot 09_block; xdotool keyup w

for i in 10 11 12 13 14 15; do
    hold Right 1.2
    tap a 0.15; tap s 0.15; tap x 0.15; tap z 0.15
    shot "${i}_fight"
done

# push all the way to the arena and into the boss
# Mash a wide key set: Flycast's default keyboard map isn't documented
# here, and enemies flank, so swing while facing both ways or the wave
# never clears and the arena is never reached.
mash() {
    for k in a s d f x c z v Shift_L Control_L; do
        xdotool keydown "$k"; sleep 0.05; xdotool keyup "$k"
    done
}
for i in $(seq 16 22); do
    hold Right 1.6; mash
    hold Left 0.5;  mash
    hold Right 0.8; mash
    shot "$(printf %02d $i)_advance"
done

echo "done: $(ls "$OUT"/*.png 2>/dev/null | wc -l) screenshots"
