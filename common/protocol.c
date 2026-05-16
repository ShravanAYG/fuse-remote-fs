/*
 * protocol.c — Wire protocol serialization/deserialization for NetFS
 */

#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Reliable I/O ────────────────────────────────────────────────── */

int netfs_read_full(int fd, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char *)buf + total, len - total);
        if (n <= 0) {
            return -1; /* EOF or error */
        }
        total += (size_t)n;
    }
    return 0;
}

int netfs_write_full(int fd, const void *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, (const char *)buf + total, len - total);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

/* ── Request Send/Recv ───────────────────────────────────────────── */

int netfs_send_request(int fd, uint8_t opcode,
                       const void *payload, uint32_t payload_len)
{
    netfs_req_header_t hdr;
    hdr.opcode      = opcode;
    hdr.payload_len = htonl(payload_len);

    if (netfs_write_full(fd, &hdr, sizeof(hdr)) < 0)
        return -1;

    if (payload_len > 0 && payload) {
        if (netfs_write_full(fd, payload, payload_len) < 0)
            return -1;
    }
    return 0;
}

int netfs_recv_request(int fd, uint8_t *opcode_out,
                       void **payload_out, uint32_t *payload_len_out)
{
    netfs_req_header_t hdr;
    if (netfs_read_full(fd, &hdr, sizeof(hdr)) < 0)
        return -1;

    *opcode_out      = hdr.opcode;
    *payload_len_out = ntohl(hdr.payload_len);

    if (*payload_len_out > 0) {
        /* Sanity cap: 64 MiB max payload */
        if (*payload_len_out > 64 * 1024 * 1024) {
            return -1;
        }
        *payload_out = malloc(*payload_len_out);
        if (!*payload_out)
            return -1;
        if (netfs_read_full(fd, *payload_out, *payload_len_out) < 0) {
            free(*payload_out);
            *payload_out = NULL;
            return -1;
        }
    } else {
        *payload_out = NULL;
    }
    return 0;
}

/* ── Response Send/Recv ──────────────────────────────────────────── */

int netfs_send_response(int fd, int32_t status,
                        const void *payload, uint32_t payload_len)
{
    netfs_resp_header_t hdr;
    hdr.status      = htonl((uint32_t)status);
    hdr.payload_len = htonl(payload_len);

    if (netfs_write_full(fd, &hdr, sizeof(hdr)) < 0)
        return -1;

    if (payload_len > 0 && payload) {
        if (netfs_write_full(fd, payload, payload_len) < 0)
            return -1;
    }
    return 0;
}

int netfs_recv_response(int fd, int32_t *status_out,
                        void **payload_out, uint32_t *payload_len_out)
{
    netfs_resp_header_t hdr;
    if (netfs_read_full(fd, &hdr, sizeof(hdr)) < 0)
        return -1;

    *status_out      = (int32_t)ntohl((uint32_t)hdr.status);
    *payload_len_out = ntohl(hdr.payload_len);

    if (*payload_len_out > 0) {
        if (*payload_len_out > 64 * 1024 * 1024) {
            return -1;
        }
        *payload_out = malloc(*payload_len_out);
        if (!*payload_out)
            return -1;
        if (netfs_read_full(fd, *payload_out, *payload_len_out) < 0) {
            free(*payload_out);
            *payload_out = NULL;
            return -1;
        }
    } else {
        *payload_out = NULL;
    }
    return 0;
}

/* ── Stat Conversion ─────────────────────────────────────────────── */

void netfs_stat_to_wire(const struct stat *st, netfs_stat_t *wire)
{
    memset(wire, 0, sizeof(*wire));
    wire->mode      = st->st_mode;
    wire->nlink     = (uint32_t)st->st_nlink;
    wire->uid       = st->st_uid;
    wire->gid       = st->st_gid;
    wire->size      = st->st_size;
    wire->blocks    = (uint64_t)st->st_blocks;
    wire->blksize   = (uint32_t)st->st_blksize;
    wire->atime_sec  = st->st_atim.tv_sec;
    wire->atime_nsec = st->st_atim.tv_nsec;
    wire->mtime_sec  = st->st_mtim.tv_sec;
    wire->mtime_nsec = st->st_mtim.tv_nsec;
    wire->ctime_sec  = st->st_ctim.tv_sec;
    wire->ctime_nsec = st->st_ctim.tv_nsec;
}

void netfs_stat_from_wire(const netfs_stat_t *wire, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode    = wire->mode;
    st->st_nlink   = wire->nlink;
    st->st_uid     = wire->uid;
    st->st_gid     = wire->gid;
    st->st_size    = wire->size;
    st->st_blocks  = (blkcnt_t)wire->blocks;
    st->st_blksize = (blksize_t)wire->blksize;
    st->st_atim.tv_sec  = wire->atime_sec;
    st->st_atim.tv_nsec = wire->atime_nsec;
    st->st_mtim.tv_sec  = wire->mtime_sec;
    st->st_mtim.tv_nsec = wire->mtime_nsec;
    st->st_ctim.tv_sec  = wire->ctime_sec;
    st->st_ctim.tv_nsec = wire->ctime_nsec;
}
