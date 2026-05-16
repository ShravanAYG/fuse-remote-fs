#!/bin/bash
# start server

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PORT="${1:-9000}"
BACKING_DIR="${2:-$SCRIPT_DIR/shared}"

# Build if needed
if [ ! -f "$PROJECT_DIR/netfs_server" ]; then
    echo "[*] Building project..."
    make -C "$PROJECT_DIR"
fi

# Create backing dir if it doesn't exist
mkdir -p "$BACKING_DIR"

echo "Starting NetFS Server on port $PORT with backing dir $BACKING_DIR"
echo "PID: $$ - Press Ctrl+C to stop."

exec "$PROJECT_DIR/netfs_server" "$PORT" "$BACKING_DIR"
