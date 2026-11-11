#!/bin/sh
# Starts the umod4 server.
cd "$(dirname "$0")" || exit 1
if ! command -v uv >/dev/null 2>&1; then
    echo "uv is not installed, or this shell cannot find it." >&2
    echo "Follow STEP 1 in README.txt, open a new terminal, and try again." >&2
    exit 1
fi
exec uv run --only-group server tools/server/umod4_server.py "$@"
