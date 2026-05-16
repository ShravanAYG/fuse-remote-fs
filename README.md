# NetFS

Quick and dirty network filesystem built with FUSE3 and TCP sockets. 

Basically, the **server** shares a folder over a socket, and the **client** mounts it locally so you can use it like a normal disk.

## Architecture

Apps -> Kernel -> FUSE -> [netfs_client] ---TCP---> [netfs_server] -> Backing Dir

## How to build

You'll need `gcc` and `libfuse3-dev`.

```bash
make
```

This gives you `netfs_server` and `netfs_client`.

## How to use

### 1. Start the server

```bash
# Serve /some/dir on port 9000
./netfs_server 9000 /some/dir
```

### 2. Mount it

```bash
mkdir -p ./mnt

# Mount it (foreground mode)
./netfs_client --host=127.0.0.1 --port=9000 ./mnt -f
```

### 3. Do stuff

```bash
ls ./mnt
echo "test" > ./mnt/file.txt
cat ./mnt/file.txt
```

### 4. Unmount

```bash
fusermount3 -u ./mnt
```

## Stuff it supports

- getattr
- readdir
- open/read/write
- create/mkdir
- unlink/rmdir
- rename
- truncate
- chmod/utimens

## Protocol

Just a simple length-prefixed binary thing:

- **Request:** `[opcode:1B][len:4B][payload:var]`
- **Response:** `[status:4B][len:4B][payload:var]`

Status 0 means OK, negative is -errno.
