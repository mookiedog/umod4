#!/bin/bash
# Convenience script to run umod4_server.py with the project's virtual environment

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_PYTHON="${SCRIPT_DIR}/../../build/.venv/bin/python3"

if [ ! -f "$VENV_PYTHON" ]; then
    echo "Error: Virtual environment not found at $VENV_PYTHON"
    echo "Please run 'cmake' in the build directory first to create the venv and install dependencies."
    exit 1
fi

exec "$VENV_PYTHON" "${SCRIPT_DIR}/umod4_server.py" "$@"
