#!/bin/bash
# run the api example

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
HOST="${1:-127.0.0.1}"
PORT="${2:-9000}"

# Build if needed
if [ ! -f "$PROJECT_DIR/netfs_example" ]; then
    echo "[*] Building project..."
    make -C "$PROJECT_DIR"
fi

echo "Running NetFS C API Example (Server: $HOST:$PORT)"

exec "$PROJECT_DIR/netfs_example" "$HOST" "$PORT"
