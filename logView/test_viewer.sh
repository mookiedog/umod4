#!/bin/bash

# Test script to open the viewer with a log file
# Usage: ./test_viewer.sh [logfile]

VIEWER="$HOME/projects/umod4/logView/viewer_prototype.html"
LOGFILE="${1:-$HOME/logs/2025-11-24/log.3}"

if [ ! -f "$LOGFILE" ]; then
    echo "Error: Log file not found: $LOGFILE"
    exit 1
fi

echo "=========================================="
echo "umod4 Log Viewer - Test Script"
echo "=========================================="
echo "Viewer:   $VIEWER"
echo "Log file: $LOGFILE"
echo ""

# Detect if running in WSL
if grep -q Microsoft /proc/version 2>/dev/null || grep -q microsoft /proc/version 2>/dev/null; then
    echo "Running in WSL - opening in Windows browser..."
    echo ""
    echo "NOTE: Due to browser security, you must manually load the log file."
    echo ""
    echo "Instructions:"
    echo "1. Browser will open with the viewer"
    echo "2. Click 'Open Log File' button"
    echo "3. Navigate to and select:"
    echo "   \\\\wsl\$\\Ubuntu$LOGFILE"
    echo ""
    echo "Or drag and drop from Windows Explorer:"
    echo "   \\\\wsl\$\\Ubuntu$LOGFILE"
    echo ""

    # Use Windows to open in default browser
    # Convert WSL path to Windows path for browser
    WIN_VIEWER=$(wslpath -w "$VIEWER")
    cmd.exe /c start "" "$WIN_VIEWER" 2>/dev/null &

    # Give helpful path for copying log to Windows if needed
    echo "Alternative: Copy log to Windows first:"
    echo "  cp \"$LOGFILE\" /mnt/c/Users/\$USER/Downloads/"
    echo "  (then load from C:\\Users\\$USER\\Downloads in browser)"
    echo ""

elif command -v xdg-open &> /dev/null; then
    echo "Opening in Linux browser..."
    xdg-open "$VIEWER"

elif command -v firefox &> /dev/null; then
    echo "Opening in Firefox..."
    firefox "$VIEWER" &

elif command -v chrome &> /dev/null; then
    echo "Opening in Chrome..."
    chrome "$VIEWER" &

else
    echo "Could not find browser. Please open manually:"
    echo "  file://$VIEWER"
    echo ""
    echo "Or start a local web server:"
    echo "  cd $(dirname $VIEWER)"
    echo "  python3 -m http.server 8080"
    echo "  Then browse to: http://localhost:8080/$(basename $VIEWER)"
fi

echo ""
echo "Expected results:"
echo "  File Size:   ~38 KB"
echo "  Events:      ~1500"
echo "  Duration:    ~0.0s"
echo "  Parse Time:  <0.5s"
echo ""
echo "First events should show:"
echo "  [     1 @     0.0000s]: WP_VER:  0"
echo "  [     2 @     0.0000s]: EP_VER:  0"
echo "  [    10 @     0.0000s]: LOAD:    8796539"
echo "  [    21 @     0.0000s]: STAT:    ERR_NOERR"
echo "=========================================="
