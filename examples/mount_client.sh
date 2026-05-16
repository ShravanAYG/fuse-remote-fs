#!/bin/bash
# mount_client.sh — Mount the NetFS FUSE client
#
# Usage: ./mount_client.sh [host] [port] [mountpoint]
#   host       — server IP (default: 127.0.0.1)
#   port       — server port (default: 9000)
#   mountpoint — local mount path (default: ./mnt)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
HOST="${1:-127.0.0.1}"
PORT="${2:-9000}"
MOUNTPOINT="${3:-$SCRIPT_DIR/mnt}"

# Build if needed
if [ ! -f "$PROJECT_DIR/netfs_client" ]; then
    echo "[*] Building project..."
    make -C "$PROJECT_DIR"
fi

# Create mountpoint
mkdir -p "$MOUNTPOINT"

echo "Mounting NetFS Client to $MOUNTPOINT (Server: $HOST:$PORT)"
echo "Running in foreground. Press Ctrl+C to unmount."

exec "$PROJECT_DIR/netfs_client" --host="$HOST" --port="$PORT" "$MOUNTPOINT" -f
