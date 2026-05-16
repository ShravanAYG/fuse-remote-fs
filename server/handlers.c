/*
 * handlers.c — Server-side operation handlers for NetFS
 *
 * Each handler receives the already-deserialized payload from the client,
 * performs the real filesystem operation on the backing directory,
 * and sends the response back over the socket.
 *
 * Payload format conventions (all multi-byte fields in host order since
 * both sides serialize/deserialize explicitly):
 *
 * For most ops:  [ path_len(2B) ][ path(varN) ] + op-specific fields
 * For rename:    [ path_len(2B) ][ old_path ][ path_len(2B) ][ new_path ]
 * For read:      [ path_len(2B) ][ path ][ offset(8B) ][ size(4B) ]
 * For write:     [ path_len(2B) ][ path ][ offset(8B) ][ data_len(4B) ][ data ]
 */

#include "handlers.h"
#include "../common/protocol.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

/*
 * Build the full path: base_dir + relative_path
 * Returns a malloc'd string the caller must free.
 */
static char *build_full_path(const char *base_dir, const char *rel_path)
{
    size_t blen = strlen(base_dir);
    size_t rlen = strlen(rel_path);
    /* +2 for '/' and '\0' */
    char *full = malloc(blen + rlen + 2);
    if (!full) return NULL;

    memcpy(full, base_dir, blen);
    /* Ensure no double slash */
    if (blen > 0 && base_dir[blen - 1] == '/') {
        if (rlen > 0 && rel_path[0] == '/') {
            memcpy(full + blen, rel_path + 1, rlen);
            full[blen + rlen - 1] = '\0';
        } else {
            memcpy(full + blen, rel_path, rlen);
            full[blen + rlen] = '\0';
        }
    } else {
        if (rlen > 0 && rel_path[0] == '/') {
            memcpy(full + blen, rel_path, rlen);
            full[blen + rlen] = '\0';
        } else {
            full[blen] = '/';
            memcpy(full + blen + 1, rel_path, rlen);
            full[blen + rlen + 1] = '\0';
        }
    }
    return full;
}

/*
 * Extract a path from the payload at the given offset.
 * Format: [ path_len : 2 bytes ][ path : path_len bytes ]
 * Returns the path as a null-terminated malloc'd string.
 * Updates *offset to point past the path.
 */
static char *extract_path(const void *payload, uint32_t payload_len,
                          uint32_t *offset)
{
    if (*offset + 2 > payload_len) return NULL;

    const uint8_t *p = (const uint8_t *)payload;
    uint16_t path_len = (uint16_t)(p[*offset] << 8 | p[*offset + 1]);
    *offset += 2;

    if (*offset + path_len > payload_len) return NULL;

    char *path = malloc(path_len + 1);
    if (!path) return NULL;
    memcpy(path, p + *offset, path_len);
    path[path_len] = '\0';
    *offset += path_len;
    return path;
}

/* ── GETATTR ─────────────────────────────────────────────────────── */

void handle_getattr(int client_fd, const char *base_dir,
                    const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    struct stat st;
    int ret = lstat(full, &st);
    free(full);

    if (ret < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }

    netfs_stat_t wire;
    netfs_stat_to_wire(&st, &wire);
    netfs_send_response(client_fd, 0, &wire, sizeof(wire));
}

/* ── READDIR ─────────────────────────────────────────────────────── */

void handle_readdir(int client_fd, const char *base_dir,
                    const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    DIR *dp = opendir(full);
    free(full);
    if (!dp) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }

    /*
     * Build a response payload: sequence of [ name_len(2B) ][ name ]
     * We'll build it in a dynamic buffer.
     */
    size_t buf_cap = 4096;
    size_t buf_len = 0;
    char *buf = malloc(buf_cap);
    if (!buf) {
        closedir(dp);
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        uint16_t nlen = (uint16_t)strlen(de->d_name);
        size_t need = 2 + nlen;
        while (buf_len + need > buf_cap) {
            buf_cap *= 2;
            char *tmp = realloc(buf, buf_cap);
            if (!tmp) {
                free(buf);
                closedir(dp);
                netfs_send_response(client_fd, -ENOMEM, NULL, 0);
                return;
            }
            buf = tmp;
        }
        buf[buf_len]     = (char)(nlen >> 8);
        buf[buf_len + 1] = (char)(nlen & 0xFF);
        memcpy(buf + buf_len + 2, de->d_name, nlen);
        buf_len += need;
    }
    closedir(dp);

    netfs_send_response(client_fd, 0, buf, (uint32_t)buf_len);
    free(buf);
}

/* ── OPEN ────────────────────────────────────────────────────────── */

void handle_open(int client_fd, const char *base_dir,
                 const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    /* Extract flags (4 bytes) */
    int32_t flags = 0;
    if (off + 4 <= payload_len) {
        const uint8_t *p = (const uint8_t *)payload;
        flags = (int32_t)((p[off] << 24) | (p[off+1] << 16) |
                          (p[off+2] << 8)  | p[off+3]);
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    /* Just check if the file can be opened with those flags */
    int fd = open(full, flags);
    free(full);

    if (fd < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }
    close(fd);
    netfs_send_response(client_fd, 0, NULL, 0);
}

/* ── READ ────────────────────────────────────────────────────────── */

void handle_read(int client_fd, const char *base_dir,
                 const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    /* Extract offset (8 bytes) and size (4 bytes) */
    if (off + 12 > payload_len) {
        free(rel_path);
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }
    const uint8_t *p = (const uint8_t *)payload;
    int64_t file_offset = 0;
    for (int i = 0; i < 8; i++)
        file_offset = (file_offset << 8) | p[off + i];
    off += 8;

    uint32_t read_size = (uint32_t)((p[off] << 24) | (p[off+1] << 16) |
                                    (p[off+2] << 8)  | p[off+3]);
    off += 4;

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int fd = open(full, O_RDONLY);
    free(full);
    if (fd < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }

    /* Cap read to 4 MiB */
    if (read_size > 4 * 1024 * 1024)
        read_size = 4 * 1024 * 1024;

    char *data = malloc(read_size);
    if (!data) {
        close(fd);
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    ssize_t n = pread(fd, data, read_size, (off_t)file_offset);
    close(fd);

    if (n < 0) {
        free(data);
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }

    netfs_send_response(client_fd, (int32_t)n, data, (uint32_t)n);
    free(data);
}

/* ── WRITE ───────────────────────────────────────────────────────── */

void handle_write(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    /* Extract offset (8 bytes) and data_len (4 bytes) */
    if (off + 12 > payload_len) {
        free(rel_path);
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }
    const uint8_t *p = (const uint8_t *)payload;
    int64_t file_offset = 0;
    for (int i = 0; i < 8; i++)
        file_offset = (file_offset << 8) | p[off + i];
    off += 8;

    uint32_t data_len = (uint32_t)((p[off] << 24) | (p[off+1] << 16) |
                                   (p[off+2] << 8)  | p[off+3]);
    off += 4;

    if (off + data_len > payload_len) {
        free(rel_path);
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int fd = open(full, O_WRONLY);
    free(full);
    if (fd < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }

    ssize_t n = pwrite(fd, p + off, data_len, (off_t)file_offset);
    close(fd);

    if (n < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }

    /* Return bytes written in status field */
    netfs_send_response(client_fd, (int32_t)n, NULL, 0);
}

/* ── CREATE ──────────────────────────────────────────────────────── */

void handle_create(int client_fd, const char *base_dir,
                   const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    /* Extract mode (4 bytes) */
    uint32_t mode = 0644;
    if (off + 4 <= payload_len) {
        const uint8_t *p = (const uint8_t *)payload;
        mode = (uint32_t)((p[off] << 24) | (p[off+1] << 16) |
                          (p[off+2] << 8)  | p[off+3]);
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int fd = open(full, O_CREAT | O_WRONLY | O_TRUNC, (mode_t)mode);
    free(full);

    if (fd < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }
    close(fd);
    netfs_send_response(client_fd, 0, NULL, 0);
}

/* ── MKDIR ───────────────────────────────────────────────────────── */

void handle_mkdir(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    uint32_t mode = 0755;
    if (off + 4 <= payload_len) {
        const uint8_t *p = (const uint8_t *)payload;
        mode = (uint32_t)((p[off] << 24) | (p[off+1] << 16) |
                          (p[off+2] << 8)  | p[off+3]);
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = mkdir(full, (mode_t)mode);
    free(full);

    if (ret < 0) {
        netfs_send_response(client_fd, -errno, NULL, 0);
        return;
    }
    netfs_send_response(client_fd, 0, NULL, 0);
}

/* ── UNLINK ──────────────────────────────────────────────────────── */

void handle_unlink(int client_fd, const char *base_dir,
                   const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = unlink(full);
    free(full);

    netfs_send_response(client_fd, ret < 0 ? -errno : 0, NULL, 0);
}

/* ── RMDIR ───────────────────────────────────────────────────────── */

void handle_rmdir(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = rmdir(full);
    free(full);

    netfs_send_response(client_fd, ret < 0 ? -errno : 0, NULL, 0);
}

/* ── RENAME ──────────────────────────────────────────────────────── */

void handle_rename(int client_fd, const char *base_dir,
                   const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *old_path = extract_path(payload, payload_len, &off);
    if (!old_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *new_path = extract_path(payload, payload_len, &off);
    if (!new_path) {
        free(old_path);
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    char *full_old = build_full_path(base_dir, old_path);
    char *full_new = build_full_path(base_dir, new_path);
    free(old_path);
    free(new_path);

    if (!full_old || !full_new) {
        free(full_old);
        free(full_new);
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = rename(full_old, full_new);
    free(full_old);
    free(full_new);

    netfs_send_response(client_fd, ret < 0 ? -errno : 0, NULL, 0);
}

/* ── TRUNCATE ────────────────────────────────────────────────────── */

void handle_truncate(int client_fd, const char *base_dir,
                     const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    /* Extract new size (8 bytes) */
    int64_t new_size = 0;
    if (off + 8 <= payload_len) {
        const uint8_t *p = (const uint8_t *)payload;
        for (int i = 0; i < 8; i++)
            new_size = (new_size << 8) | p[off + i];
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = truncate(full, (off_t)new_size);
    free(full);

    netfs_send_response(client_fd, ret < 0 ? -errno : 0, NULL, 0);
}

/* ── CHMOD ───────────────────────────────────────────────────────── */

void handle_chmod(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    uint32_t mode = 0;
    if (off + 4 <= payload_len) {
        const uint8_t *p = (const uint8_t *)payload;
        mode = (uint32_t)((p[off] << 24) | (p[off+1] << 16) |
                          (p[off+2] << 8)  | p[off+3]);
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = chmod(full, (mode_t)mode);
    free(full);

    netfs_send_response(client_fd, ret < 0 ? -errno : 0, NULL, 0);
}

/* ── UTIMENS ─────────────────────────────────────────────────────── */

void handle_utimens(int client_fd, const char *base_dir,
                    const void *payload, uint32_t payload_len)
{
    uint32_t off = 0;
    char *rel_path = extract_path(payload, payload_len, &off);
    if (!rel_path) {
        netfs_send_response(client_fd, -EINVAL, NULL, 0);
        return;
    }

    /* Extract atime_sec(8) + atime_nsec(8) + mtime_sec(8) + mtime_nsec(8) = 32 bytes */
    struct timespec ts[2] = {{0, 0}, {0, 0}};
    if (off + 32 <= payload_len) {
        const uint8_t *p = (const uint8_t *)payload;
        int64_t val;
        /* atime_sec */
        val = 0;
        for (int i = 0; i < 8; i++) val = (val << 8) | p[off + i];
        ts[0].tv_sec = val; off += 8;
        /* atime_nsec */
        val = 0;
        for (int i = 0; i < 8; i++) val = (val << 8) | p[off + i];
        ts[0].tv_nsec = val; off += 8;
        /* mtime_sec */
        val = 0;
        for (int i = 0; i < 8; i++) val = (val << 8) | p[off + i];
        ts[1].tv_sec = val; off += 8;
        /* mtime_nsec */
        val = 0;
        for (int i = 0; i < 8; i++) val = (val << 8) | p[off + i];
        ts[1].tv_nsec = val; off += 8;
    }

    char *full = build_full_path(base_dir, rel_path);
    free(rel_path);
    if (!full) {
        netfs_send_response(client_fd, -ENOMEM, NULL, 0);
        return;
    }

    int ret = utimensat(AT_FDCWD, full, ts, AT_SYMLINK_NOFOLLOW);
    free(full);

    netfs_send_response(client_fd, ret < 0 ? -errno : 0, NULL, 0);
}
