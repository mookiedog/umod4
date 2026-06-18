#!/usr/bin/env bash
#
# Nightly automated test run for umod4.
#
# Builds the firmware from the current checkout, runs the full hardware
# test suite, and sends a pass/fail notification via ntfy.sh.
#
# Intended to be invoked nightly by Windows Task Scheduler via:
#   wsl.exe -d Ubuntu -- bash -lic "cd ~/projects/umod4 && tests/nightly_run.sh"
# (see tools/setup_nightly_tests)
#
# Note: bash -lic, not just -lc or -ic -- need BOTH login and interactive:
#  -l (login)       sources ~/.profile, which adds ~/.local/bin (picotool,
#                    m68hc11-elf, etc.) to PATH and sources ~/.bashrc.
#  -i (interactive) makes ~/.bashrc run past its interactive-only guard,
#                    which otherwise `return`s before PICO_SDK_PATH,
#                    ~/.umod4_test_env, etc. are set.
# Without -i, PICO_SDK_PATH is missing (picotool/CMake configure fails).
# Without -l, ~/.local/bin is missing from PATH (picotool not found).
#
# WiFi credentials, device name, and NTFY_TOPIC come from
# ~/.umod4_test_env, sourced by ~/.bashrc (and redundantly here in case
# this script is ever invoked a different way).
#
# To enable ntfy.sh notifications, set NTFY_TOPIC in ~/.umod4_test_env
# and subscribe to that topic at https://ntfy.sh/app. If unset,
# notifications are silently skipped.
#
# IMPORTANT: this tests whatever is currently checked out in this working
# copy. Leave it on `main` with no uncommitted changes for the results to
# be meaningful.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

[ -f ~/.umod4_test_env ] && source ~/.umod4_test_env

TS="$(date '+%Y%m%d_%H%M%S')"
LOG_DIR="$PROJECT_ROOT/build/test_reports"
mkdir -p "$LOG_DIR"
BUILD_LOG="$LOG_DIR/nightly_${TS}_build.log"
TEST_LOG="$LOG_DIR/nightly_${TS}_test.log"

notify() {
    local title="$1" priority="$2" message="$3"
    local notify_log="$LOG_DIR/nightly_${TS}_notify.log"

    if [[ -z "${NTFY_TOPIC:-}" ]]; then
        echo "$(date '+%Y-%m-%d %H:%M:%S') NTFY_TOPIC not set -- notification skipped" >> "$notify_log"
        return
    fi

    if curl -fsS --max-time 15 \
        -H "Title: $title" \
        -H "Priority: $priority" \
        -d "$message" \
        "https://ntfy.sh/$NTFY_TOPIC" >> "$notify_log" 2>&1
    then
        echo "$(date '+%Y-%m-%d %H:%M:%S') notify: curl OK (topic=$NTFY_TOPIC)" >> "$notify_log"
    else
        echo "$(date '+%Y-%m-%d %H:%M:%S') notify: curl FAILED rc=$? (topic=$NTFY_TOPIC)" >> "$notify_log"
    fi
}

# --- Build -----------------------------------------------------------------
if [[ ! -f "$PROJECT_ROOT/build/build.ninja" ]]; then
    notify "umod4 nightly: BUILD FAIL" "urgent" \
        "build/build.ninja not found -- has the build been configured?"
    exit 1
fi

if ! ninja -C "$PROJECT_ROOT/build" > "$BUILD_LOG" 2>&1; then
    notify "umod4 nightly: BUILD FAIL" "urgent" "$(tail -n 20 "$BUILD_LOG")"
    exit 1
fi

# --- Test --------------------------------------------------------------------
if "$PROJECT_ROOT/build/.venv/bin/python3" tests/runner.py > "$TEST_LOG" 2>&1; then
    notify "umod4 nightly: PASS" "default" \
        "$(date '+%Y-%m-%d %H:%M') - all suites passed."
    exit 0
else
    FAILURES_FILE="$LOG_DIR/latest_failures.txt"
    if [[ -s "$FAILURES_FILE" ]]; then
        DETAIL="$(cat "$FAILURES_FILE")"
    else
        DETAIL="(no per-test failure detail -- see build/test_reports/latest.md)"
    fi
    notify "umod4 nightly: FAIL" "urgent" \
        "$(date '+%Y-%m-%d %H:%M')
$DETAIL"
    exit 1
fi
