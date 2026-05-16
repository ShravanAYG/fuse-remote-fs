/*
 * netfs_api.h — NetFS Client API
 *
 * A simple C library for accessing files on a NetFS server.
 * Any C program can link against this to read/write remote files
 * into local char buffers without mounting a FUSE filesystem.
 *
 * Example:
 *   netfs_ctx *ctx = netfs_connect("127.0.0.1", 9000);
 *   char buf[4096];
 *   int n = netfs_read_file(ctx, "/hello.txt", buf, sizeof(buf), 0);
 *   netfs_write_file(ctx, "/hello.txt", "new data", 8, 0);
 *   netfs_disconnect(ctx);
 */

#ifndef NETFS_API_H
#define NETFS_API_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

/* Opaque connection context */
typedef struct netfs_ctx netfs_ctx;

/* ── Connection ──────────────────────────────────────────────────── */

/*
 * Connect to a NetFS server.
 * Returns a context handle, or NULL on failure.
 */
netfs_ctx *netfs_connect(const char *host, int port);

/*
 * Disconnect and free the context.
 */
void netfs_disconnect(netfs_ctx *ctx);

/* ── File I/O ────────────────────────────────────────────────────── */

/*
 * Read up to `size` bytes from `path` at `offset` into `buf`.
 * Returns bytes read on success, or negative errno on failure.
 */
int netfs_read_file(netfs_ctx *ctx, const char *path,
                    char *buf, size_t size, off_t offset);

/*
 * Write `size` bytes from `buf` to `path` at `offset`.
 * Returns bytes written on success, or negative errno on failure.
 */
int netfs_write_file(netfs_ctx *ctx, const char *path,
                     const char *buf, size_t size, off_t offset);

/*
 * Create a new file at `path` with the given permissions.
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_create_file(netfs_ctx *ctx, const char *path, uint32_t mode);

/*
 * Delete a file at `path`.
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_delete_file(netfs_ctx *ctx, const char *path);

/* ── Directory Operations ────────────────────────────────────────── */

/*
 * Create a directory at `path` with the given permissions.
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_mkdir(netfs_ctx *ctx, const char *path, uint32_t mode);

/*
 * Remove a directory at `path`.
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_rmdir(netfs_ctx *ctx, const char *path);

/*
 * List directory contents at `path`.
 * Fills `names` array with up to `max_entries` null-terminated strings.
 * Each string is malloc'd — caller must free each entry.
 * Returns number of entries on success, or negative errno on failure.
 */
int netfs_list_dir(netfs_ctx *ctx, const char *path,
                   char **names, int max_entries);

/* ── Metadata ────────────────────────────────────────────────────── */

/*
 * Get file/directory attributes (stat).
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_stat(netfs_ctx *ctx, const char *path, struct stat *st);

/*
 * Rename/move a file or directory.
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_rename(netfs_ctx *ctx, const char *old_path, const char *new_path);

/*
 * Truncate a file to the given size.
 * Returns 0 on success, or negative errno on failure.
 */
int netfs_truncate(netfs_ctx *ctx, const char *path, off_t size);

#endif /* NETFS_API_H */
