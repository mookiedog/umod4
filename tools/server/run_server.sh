#!/bin/bash
# Convenience script to run umod4_server.py with the project's uv-managed Python environment

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v uv >/dev/null 2>&1; then
    echo "Error: uv not found. See BUILDING.md for how to install it."
    exit 1
fi

exec uv run --project "${SCRIPT_DIR}/../.." "${SCRIPT_DIR}/umod4_server.py" "$@"
