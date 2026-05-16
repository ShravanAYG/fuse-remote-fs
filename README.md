# NetFS - FUSE Network Filesystem

A client/server network filesystem built with FUSE3 and TCP sockets in C.
The **server** exposes a real directory over the network, and the **client**
mounts a virtual FUSE directory that transparently forwards all file
operations to the server.

## Architecture

```
User Apps ─→ Kernel VFS ─→ /dev/fuse ─→ [netfs_client] ──TCP──→ [netfs_server] ─→ Backing Dir
```

## Supported Operations

| Operation | Description               |
|-----------|---------------------------|
| getattr   | File/dir metadata (stat)  |
| readdir   | List directory contents   |
| open      | Open a file               |
| read      | Read file data            |
| write     | Write file data           |
| create    | Create a new file         |
| mkdir     | Create a directory        |
| unlink    | Delete a file             |
| rmdir     | Remove a directory        |
| rename    | Rename/move a file or dir |
| truncate  | Resize a file             |
| chmod     | Change file permissions   |
| utimens   | Update timestamps         |

## Building

```bash
# Requires: gcc, libfuse3-dev (or fuse3 on Arch)
make
```

This produces two binaries: `netfs_server` and `netfs_client`.

## Usage

### 1. Start the server

```bash
# Serve the contents of /path/to/shared/dir on port 9000
./netfs_server 9000 /path/to/shared/dir
```

### 2. Mount the client

```bash
# Create a mountpoint
mkdir -p /tmp/netfs_mount

# Mount (foreground mode for debugging)
./netfs_client --host=127.0.0.1 --port=9000 /tmp/netfs_mount -f

# Or run in background (daemon mode)
./netfs_client --host=127.0.0.1 --port=9000 /tmp/netfs_mount
```

### 3. Use the filesystem

```bash
ls /tmp/netfs_mount
echo "hello world" > /tmp/netfs_mount/test.txt
cat /tmp/netfs_mount/test.txt
mkdir /tmp/netfs_mount/subdir
rm /tmp/netfs_mount/test.txt
```

### 4. Unmount

```bash
fusermount3 -u /tmp/netfs_mount
```

## Project Structure

```
├── Makefile              # Build system
├── README.md             # This file
├── common/
│   ├── protocol.h        # Wire protocol definitions
│   └── protocol.c        # Serialization/deserialization
├── server/
│   ├── server.c          # TCP server main loop
│   ├── handlers.h        # Handler declarations
│   └── handlers.c        # Filesystem operation handlers
└── client/
    └── client.c          # FUSE client daemon
```

## Wire Protocol

Simple length-prefixed binary protocol:

**Request:** `[opcode:1B][payload_len:4B][payload:varN]`  
**Response:** `[status:4B][payload_len:4B][payload:varN]`

- Status = 0 on success, negative errno on failure
- All multi-byte integers in network byte order for headers,
  big-endian for payload fields
