#!/bin/bash
# demo.sh — Full end-to-end demo: starts server, runs API example, cleans up
#
# Usage: ./demo.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PORT=9000
BACKING_DIR="$SCRIPT_DIR/shared"
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
    fi
    rm -rf "$BACKING_DIR"
}
trap cleanup EXIT

echo "Building..."
make -C "$PROJECT_DIR" > /dev/null 2>&1
echo "Built netfs_server, netfs_client, netfs_example"

# Start server
echo "Starting server on port $PORT (backing: $BACKING_DIR)..."
mkdir -p "$BACKING_DIR"
echo "pre-existing content" > "$BACKING_DIR/readme.txt"
"$PROJECT_DIR/netfs_server" "$PORT" "$BACKING_DIR" &
SERVER_PID=$!
sleep 0.5
echo "Server running (PID $SERVER_PID)"

# Run API example
echo "Running C API example..."
"$PROJECT_DIR/netfs_example" 127.0.0.1 "$PORT"

# Show backing dir state
echo "Backing directory final state:"
ls -la "$BACKING_DIR"
