#!/usr/bin/env bash
#
# run_flycast_headless.sh — Launch the Flycast Dreamcast emulator headlessly
# (under Xvfb, with Mesa llvmpipe software OpenGL) and capture a burst of PNG
# screenshots for visual QA of a built disc image or homebrew ELF.
#
# Usage:
#   tools/run_flycast_headless.sh [content-path] [-- extra flycast args...]
#
#   content-path   Path to a .cdi / .gdi / .chd / .cue disc image, or a
#                   homebrew .elf. Optional — if omitted, flycast boots to
#                   its own game-browser UI with an empty list, which is
#                   enough to validate the headless capture pipeline itself.
#   -- extra args   Anything after a literal "--" is forwarded verbatim to
#                   the flycast binary, e.g.:
#                     tools/run_flycast_headless.sh build/game.cdi -- \
#                       -config config:UseReios=yes
#
# Output:
#   PNG screenshots are written to build/qa_screenshots/shot_XX.png
#   (numbered, spaced a few seconds apart so booting/loading is visible).
#
# Environment overrides (all optional):
#   FLYCAST_BIN          Path to the flycast binary.
#                         Default: first of `flycast` on PATH,
#                         /usr/local/bin/flycast, then the local dev build at
#                         /home/user/flyinghead/flycast/build/flycast.
#   DISPLAY_NUM           Xvfb display number to use/start.        Default: 99
#   SCREEN_RES             Xvfb screen geometry.               Default: 1280x720x24
#   BOOT_WAIT_SECS        Seconds to wait before the first screenshot,
#                         to give flycast time to init/boot.          Default: 6
#   SHOT_COUNT            Number of screenshots to capture.           Default: 4
#   SHOT_INTERVAL_SECS    Seconds between screenshots.                Default: 3
#   HARD_TIMEOUT_SECS     Absolute wall-clock cap for the whole run,
#                         after which everything is force-killed.     Default: 120
#   OUT_DIR               Where to write screenshots.
#                         Default: <repo>/build/qa_screenshots
#
# Exit codes:
#   0  success — at least one screenshot was captured
#   2  flycast binary not found
#   3  Xvfb failed to start / no working X display
#   4  flycast process failed to start or died before any screenshot
#   5  no screenshots were captured (import/ImageMagick failure)
#
set -uo pipefail

HARD_TIMEOUT_SECS="${HARD_TIMEOUT_SECS:-120}"

# Re-exec ourselves under `timeout` as a hard wall-clock safety net, so a
# hung flycast/Xvfb can never make this script (or a CI job calling it)
# block forever. TERM is sent first (our trap below still runs cleanup),
# KILL follows 10s later if anything ignored it. Must happen before any
# argument parsing/shifting below, since it re-execs with the original "$@".
if [[ "${_RFH_INNER:-0}" != "1" ]]; then
    export _RFH_INNER=1
    exec timeout -k 10s "${HARD_TIMEOUT_SECS}" "$0" "$@"
fi

# ---------------------------------------------------------------------------
# Setup / configuration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DISPLAY_NUM="${DISPLAY_NUM:-99}"
SCREEN_RES="${SCREEN_RES:-1280x720x24}"
BOOT_WAIT_SECS="${BOOT_WAIT_SECS:-6}"
SHOT_COUNT="${SHOT_COUNT:-4}"
SHOT_INTERVAL_SECS="${SHOT_INTERVAL_SECS:-3}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/build/qa_screenshots}"

CONTENT_PATH=""
EXTRA_ARGS=()
if [[ $# -gt 0 && "$1" != "--" ]]; then
    CONTENT_PATH="$1"
    shift
fi
if [[ $# -gt 0 && "$1" == "--" ]]; then
    shift
    EXTRA_ARGS=("$@")
fi

log() { printf '[run_flycast_headless] %s\n' "$*" >&2; }
die() { log "ERROR: $*"; cleanup; exit "${2:-1}"; }

# ---------------------------------------------------------------------------
# Locate the flycast binary
# ---------------------------------------------------------------------------
resolve_flycast_bin() {
    if [[ -n "${FLYCAST_BIN:-}" && -x "${FLYCAST_BIN}" ]]; then
        echo "$FLYCAST_BIN"; return 0
    fi
    if command -v flycast >/dev/null 2>&1; then
        command -v flycast; return 0
    fi
    if [[ -x /usr/local/bin/flycast ]]; then
        echo /usr/local/bin/flycast; return 0
    fi
    if [[ -x /home/user/flyinghead/flycast/build/flycast ]]; then
        echo /home/user/flyinghead/flycast/build/flycast; return 0
    fi
    return 1
}

FLYCAST_BIN="$(resolve_flycast_bin)" || {
    log "ERROR: could not find a flycast binary."
    log "  Set FLYCAST_BIN=/path/to/flycast, or install it on PATH."
    log "  See docker/EMULATOR_README.md for how to build it."
    exit 2
}
log "Using flycast binary: $FLYCAST_BIN"

# ---------------------------------------------------------------------------
# Process bookkeeping / cleanup
# ---------------------------------------------------------------------------
XVFB_PID=""
XVFB_STARTED_BY_US=0
FLYCAST_PID=""
CLEANED_UP=0

cleanup() {
    [[ "$CLEANED_UP" -eq 1 ]] && return
    CLEANED_UP=1
    if [[ -n "$FLYCAST_PID" ]] && kill -0 "$FLYCAST_PID" 2>/dev/null; then
        log "Stopping flycast (pid $FLYCAST_PID)"
        kill "$FLYCAST_PID" 2>/dev/null
        for _ in 1 2 3 4 5; do
            kill -0 "$FLYCAST_PID" 2>/dev/null || break
            sleep 0.5
        done
        kill -9 "$FLYCAST_PID" 2>/dev/null
    fi
    if [[ "$XVFB_STARTED_BY_US" -eq 1 && -n "$XVFB_PID" ]] && kill -0 "$XVFB_PID" 2>/dev/null; then
        log "Stopping Xvfb (pid $XVFB_PID)"
        kill "$XVFB_PID" 2>/dev/null
        sleep 0.5
        kill -9 "$XVFB_PID" 2>/dev/null
    fi
    rm -f "/tmp/.X${DISPLAY_NUM}-lock" 2>/dev/null
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# Start Xvfb (unless a working DISPLAY was already provided)
# ---------------------------------------------------------------------------
TARGET_DISPLAY=":${DISPLAY_NUM}"
if [[ -n "${DISPLAY:-}" ]] && xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
    log "Reusing existing working DISPLAY=$DISPLAY"
    TARGET_DISPLAY="$DISPLAY"
else
    rm -f "/tmp/.X${DISPLAY_NUM}-lock" 2>/dev/null
    log "Starting Xvfb on $TARGET_DISPLAY ($SCREEN_RES)"
    Xvfb "$TARGET_DISPLAY" -screen 0 "$SCREEN_RES" -nolisten tcp >/tmp/xvfb_${DISPLAY_NUM}.log 2>&1 &
    XVFB_PID=$!
    XVFB_STARTED_BY_US=1

    ok=0
    for _ in $(seq 1 20); do
        if DISPLAY="$TARGET_DISPLAY" xdpyinfo >/dev/null 2>&1; then
            ok=1; break
        fi
        kill -0 "$XVFB_PID" 2>/dev/null || break
        sleep 0.25
    done
    if [[ "$ok" -ne 1 ]]; then
        log "Xvfb log:"; cat "/tmp/xvfb_${DISPLAY_NUM}.log" >&2 2>/dev/null
        die "Xvfb did not come up on $TARGET_DISPLAY" 3
    fi
fi
export DISPLAY="$TARGET_DISPLAY"

# ---------------------------------------------------------------------------
# Launch flycast
# ---------------------------------------------------------------------------
mkdir -p "$OUT_DIR"
mkdir -p /tmp/flycast-xdg-runtime && chmod 700 /tmp/flycast-xdg-runtime

FLYCAST_ARGS=()
[[ -n "$CONTENT_PATH" ]] && FLYCAST_ARGS+=("$CONTENT_PATH")
FLYCAST_ARGS+=("${EXTRA_ARGS[@]}")

log "Launching: $FLYCAST_BIN ${FLYCAST_ARGS[*]:-<no content — game-browser UI>}"
env \
    DISPLAY="$TARGET_DISPLAY" \
    SDL_VIDEODRIVER=x11 \
    SDL_AUDIODRIVER=dummy \
    XDG_RUNTIME_DIR=/tmp/flycast-xdg-runtime \
    "$FLYCAST_BIN" "${FLYCAST_ARGS[@]}" >"$OUT_DIR/flycast_stdout.log" 2>&1 &
FLYCAST_PID=$!

# Give it a moment, then confirm it's actually alive before waiting to boot.
sleep 1
if ! kill -0 "$FLYCAST_PID" 2>/dev/null; then
    log "flycast log:"; tail -n 60 "$OUT_DIR/flycast_stdout.log" >&2 2>/dev/null
    die "flycast exited immediately (pid $FLYCAST_PID)" 4
fi

log "Waiting ${BOOT_WAIT_SECS}s for boot before first screenshot..."
sleep "$BOOT_WAIT_SECS"

# ---------------------------------------------------------------------------
# Capture screenshots
# ---------------------------------------------------------------------------
CAPTURED=0
for i in $(seq 1 "$SHOT_COUNT"); do
    if ! kill -0 "$FLYCAST_PID" 2>/dev/null; then
        log "flycast is no longer running (exited early) — stopping capture at shot $i"
        break
    fi
    shot_path="$OUT_DIR/shot_$(printf '%02d' "$i").png"
    if DISPLAY="$TARGET_DISPLAY" import -window root "$shot_path" 2>>"$OUT_DIR/flycast_stdout.log"; then
        if [[ -s "$shot_path" ]]; then
            log "Captured $shot_path"
            CAPTURED=$((CAPTURED + 1))
        else
            log "WARNING: $shot_path is empty, removing"
            rm -f "$shot_path"
        fi
    else
        log "WARNING: screenshot capture failed for shot $i"
    fi
    if [[ "$i" -lt "$SHOT_COUNT" ]]; then
        sleep "$SHOT_INTERVAL_SECS"
    fi
done

cleanup

if [[ "$CAPTURED" -eq 0 ]]; then
    log "ERROR: no screenshots were captured."
    exit 5
fi

log "Done. $CAPTURED screenshot(s) written to $OUT_DIR"
exit 0
