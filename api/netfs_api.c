/*
 * netfs_api.c — NetFS Client API implementation
 *
 * Uses the existing wire protocol (common/protocol.h) to communicate
 * with the NetFS server over TCP.
 */

#include "netfs_api.h"
#include "../common/protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── Internal context ────────────────────────────────────────────── */

struct netfs_ctx {
    int  sockfd;
    char host[256];
    int  port;
};

/* ── Payload encoding helpers ────────────────────────────────────── */

static int encode_path(uint8_t *buf, size_t cap,
                       const char *path, size_t *pos)
{
    uint16_t plen = (uint16_t)strlen(path);
    if (*pos + 2 + plen > cap) return -1;
    buf[*pos]     = (uint8_t)(plen >> 8);
    buf[*pos + 1] = (uint8_t)(plen & 0xFF);
    *pos += 2;
    memcpy(buf + *pos, path, plen);
    *pos += plen;
    return 0;
}

static void encode_u32(uint8_t *buf, size_t *pos, uint32_t val)
{
    buf[*pos]     = (uint8_t)(val >> 24);
    buf[*pos + 1] = (uint8_t)(val >> 16);
    buf[*pos + 2] = (uint8_t)(val >> 8);
    buf[*pos + 3] = (uint8_t)(val);
    *pos += 4;
}

static void encode_i64(uint8_t *buf, size_t *pos, int64_t val)
{
    uint64_t v = (uint64_t)val;
    for (int i = 7; i >= 0; i--)
        buf[*pos + (7 - i)] = (uint8_t)(v >> (i * 8));
    *pos += 8;
}

/* Simple RPC: send request, receive response */
static int do_rpc(netfs_ctx *ctx, uint8_t opcode,
                  const void *req, uint32_t req_len,
                  int32_t *status, void **resp, uint32_t *resp_len)
{
    if (netfs_send_request(ctx->sockfd, opcode, req, req_len) < 0)
        return -EIO;
    if (netfs_recv_response(ctx->sockfd, status, resp, resp_len) < 0)
        return -EIO;
    return 0;
}

/* ── Connection ──────────────────────────────────────────────────── */

netfs_ctx *netfs_connect(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close(fd);
        return NULL;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    netfs_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) { close(fd); return NULL; }
    ctx->sockfd = fd;
    ctx->port   = port;
    strncpy(ctx->host, host, sizeof(ctx->host) - 1);
    return ctx;
}

void netfs_disconnect(netfs_ctx *ctx)
{
    if (!ctx) return;
    if (ctx->sockfd >= 0) close(ctx->sockfd);
    free(ctx);
}

/* ── File I/O ────────────────────────────────────────────────────── */

int netfs_read_file(netfs_ctx *ctx, const char *path,
                    char *buf, size_t size, off_t offset)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_i64(req, &pos, (int64_t)offset);
    encode_u32(req, &pos, (uint32_t)size);

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_READ, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    if (ret < 0) return ret;
    if (status < 0) { free(resp); return status; }

    /* status = bytes read by server */
    uint32_t to_copy = (uint32_t)status < rlen ? (uint32_t)status : rlen;
    if (to_copy > size) to_copy = (uint32_t)size;
    if (resp && to_copy > 0) memcpy(buf, resp, to_copy);
    free(resp);
    return (int)to_copy;
}

int netfs_write_file(netfs_ctx *ctx, const char *path,
                     const char *buf, size_t size, off_t offset)
{
    size_t hdr = 2 + strlen(path) + 8 + 4;
    size_t total = hdr + size;
    uint8_t *req = malloc(total);
    if (!req) return -ENOMEM;

    size_t pos = 0;
    encode_path(req, total, path, &pos);
    encode_i64(req, &pos, (int64_t)offset);
    encode_u32(req, &pos, (uint32_t)size);
    memcpy(req + pos, buf, size);
    pos += size;

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_WRITE, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(req);
    free(resp);
    if (ret < 0) return ret;
    return status; /* bytes written or -errno */
}

int netfs_create_file(netfs_ctx *ctx, const char *path, uint32_t mode)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_u32(req, &pos, mode);

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_CREATE, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

int netfs_delete_file(netfs_ctx *ctx, const char *path)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_UNLINK, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

/* ── Directory Operations ────────────────────────────────────────── */

int netfs_mkdir(netfs_ctx *ctx, const char *path, uint32_t mode)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_u32(req, &pos, mode);

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_MKDIR, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

int netfs_rmdir(netfs_ctx *ctx, const char *path)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_RMDIR, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

int netfs_list_dir(netfs_ctx *ctx, const char *path,
                   char **names, int max_entries)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_READDIR, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    if (ret < 0) return ret;
    if (status != 0) { free(resp); return status; }

    /* Parse: sequence of [name_len:2B][name] */
    int count = 0;
    uint32_t off = 0;
    const uint8_t *p = (const uint8_t *)resp;
    while (off + 2 <= rlen && count < max_entries) {
        uint16_t nlen = (uint16_t)(p[off] << 8 | p[off + 1]);
        off += 2;
        if (off + nlen > rlen) break;
        names[count] = malloc(nlen + 1);
        if (!names[count]) break;
        memcpy(names[count], p + off, nlen);
        names[count][nlen] = '\0';
        off += nlen;
        count++;
    }
    free(resp);
    return count;
}

/* ── Metadata ────────────────────────────────────────────────────── */

int netfs_stat(netfs_ctx *ctx, const char *path, struct stat *st)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_GETATTR, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    if (ret < 0) return ret;
    if (status != 0) { free(resp); return status; }
    if (rlen >= sizeof(netfs_stat_t))
        netfs_stat_from_wire((netfs_stat_t *)resp, st);
    free(resp);
    return 0;
}

int netfs_rename(netfs_ctx *ctx, const char *old_path, const char *new_path)
{
    uint8_t req[8192];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), old_path, &pos) < 0) return -ENAMETOOLONG;
    if (encode_path(req, sizeof(req), new_path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_RENAME, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

int netfs_truncate(netfs_ctx *ctx, const char *path, off_t size)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_i64(req, &pos, (int64_t)size);

    int32_t status;
    void *resp = NULL;
    uint32_t rlen = 0;
    int ret = do_rpc(ctx, OP_TRUNCATE, req, (uint32_t)pos,
                     &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}
