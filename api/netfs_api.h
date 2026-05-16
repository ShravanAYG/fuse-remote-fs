/*
 * netfs_api.h - Client API for NetFS
 */

#ifndef NETFS_API_H
#define NETFS_API_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

/* Opaque connection context */
typedef struct netfs_ctx netfs_ctx;



// connect/disconnect
netfs_ctx *netfs_connect(const char *host, int port);

/*
 * Disconnect and free the context.
 */
void netfs_disconnect(netfs_ctx *ctx);



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

// returns entries count. names[] entries must be freed.
int netfs_list_dir(netfs_ctx *ctx, const char *path,
                   char **names, int max_entries);



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
