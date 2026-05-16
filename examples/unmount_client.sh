#!/bin/bash
# unmount_client.sh — Unmount the NetFS FUSE client
#
# Usage: ./unmount_client.sh [mountpoint]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MOUNTPOINT="${1:-$SCRIPT_DIR/mnt}"

if mountpoint -q "$MOUNTPOINT" 2>/dev/null; then
    echo "Unmounting $MOUNTPOINT ..."
    fusermount3 -u "$MOUNTPOINT"
    echo "Unmounted."
else
    echo "$MOUNTPOINT is not mounted."
fi
