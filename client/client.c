/*
 * client.c — NetFS FUSE client daemon
 *
 * Mounts a virtual directory and forwards all VFS operations
 * to the NetFS server over TCP.
 *
 * Usage: ./netfs_client --host=<ip> --port=<port> <mountpoint> [fuse_opts]
 */

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../common/protocol.h"

/* ── Global state ────────────────────────────────────────────────── */

static char g_host[256] = "127.0.0.1";
static int  g_port      = 9000;
static int  g_sockfd    = -1;
static pthread_mutex_t g_sock_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Connection management ───────────────────────────────────────── */

static int connect_to_server(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, g_host, &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int ensure_connected(void)
{
    if (g_sockfd >= 0) return 0;
    g_sockfd = connect_to_server();
    return (g_sockfd >= 0) ? 0 : -EIO;
}

static void reconnect(void)
{
    if (g_sockfd >= 0) { close(g_sockfd); g_sockfd = -1; }
    g_sockfd = connect_to_server();
}

/* ── Payload builder helpers ─────────────────────────────────────── */

/* Encode a path: [path_len:2B][path] */
static int encode_path(uint8_t *buf, size_t cap, const char *path, size_t *pos)
{
    uint16_t plen = (uint16_t)strlen(path);
    if (*pos + 2 + plen > cap) return -1;
    buf[*pos] = (uint8_t)(plen >> 8);
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

/*
 * Thread-safe request-response round trip.
 * Sends a request, receives the response. On failure, attempts reconnect once.
 * Caller must free *resp_payload.
 */
static int do_rpc(uint8_t opcode, const void *req, uint32_t req_len,
                  int32_t *status, void **resp_payload, uint32_t *resp_len)
{
    int ret;
    pthread_mutex_lock(&g_sock_mutex);

    if (ensure_connected() < 0) {
        pthread_mutex_unlock(&g_sock_mutex);
        return -EIO;
    }

    ret = netfs_send_request(g_sockfd, opcode, req, req_len);
    if (ret < 0) {
        reconnect();
        if (g_sockfd < 0 ||
            netfs_send_request(g_sockfd, opcode, req, req_len) < 0) {
            pthread_mutex_unlock(&g_sock_mutex);
            return -EIO;
        }
    }

    ret = netfs_recv_response(g_sockfd, status, resp_payload, resp_len);
    if (ret < 0) {
        reconnect();
        pthread_mutex_unlock(&g_sock_mutex);
        return -EIO;
    }

    pthread_mutex_unlock(&g_sock_mutex);
    return 0;
}

/* ── FUSE Callbacks ──────────────────────────────────────────────── */

static int nfs_getattr(const char *path, struct stat *stbuf,
                       struct fuse_file_info *fi)
{
    (void)fi;
    uint8_t buf[4096];
    size_t pos = 0;
    if (encode_path(buf, sizeof(buf), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_GETATTR, buf, (uint32_t)pos, &status, &resp, &rlen);
    if (ret < 0) return ret;
    if (status != 0) { free(resp); return status; }
    if (rlen >= sizeof(netfs_stat_t)) {
        netfs_stat_from_wire((netfs_stat_t *)resp, stbuf);
    }
    free(resp);
    return 0;
}

static int nfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags)
{
    (void)offset; (void)fi; (void)flags;
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_READDIR, req, (uint32_t)pos, &status, &resp, &rlen);
    if (ret < 0) return ret;
    if (status != 0) { free(resp); return status; }

    /* Parse: sequence of [name_len:2B][name] */
    uint32_t off = 0;
    const uint8_t *p = (const uint8_t *)resp;
    while (off + 2 <= rlen) {
        uint16_t nlen = (uint16_t)(p[off] << 8 | p[off + 1]);
        off += 2;
        if (off + nlen > rlen) break;
        char name[256];
        size_t cplen = nlen < 255 ? nlen : 255;
        memcpy(name, p + off, cplen);
        name[cplen] = '\0';
        off += nlen;
        filler(buf, name, NULL, 0, 0);
    }
    free(resp);
    return 0;
}

static int nfs_open(const char *path, struct fuse_file_info *fi)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_u32(req, &pos, (uint32_t)fi->flags);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_OPEN, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    (void)fi;
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_i64(req, &pos, (int64_t)offset);
    encode_u32(req, &pos, (uint32_t)size);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_READ, req, (uint32_t)pos, &status, &resp, &rlen);
    if (ret < 0) return ret;
    if (status < 0) { free(resp); return status; }
    /* status = bytes read */
    uint32_t to_copy = (uint32_t)status < rlen ? (uint32_t)status : rlen;
    if (to_copy > size) to_copy = (uint32_t)size;
    if (resp && to_copy > 0) memcpy(buf, resp, to_copy);
    free(resp);
    return (int)to_copy;
}

static int nfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    (void)fi;
    size_t hdr_size = 2 + strlen(path) + 8 + 4;
    size_t total = hdr_size + size;
    uint8_t *req = malloc(total);
    if (!req) return -ENOMEM;

    size_t pos = 0;
    encode_path(req, total, path, &pos);
    encode_i64(req, &pos, (int64_t)offset);
    encode_u32(req, &pos, (uint32_t)size);
    memcpy(req + pos, buf, size);
    pos += size;

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_WRITE, req, (uint32_t)pos, &status, &resp, &rlen);
    free(req);
    free(resp);
    if (ret < 0) return ret;
    return status; /* bytes written or -errno */
}

static int nfs_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi)
{
    (void)fi;
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_u32(req, &pos, (uint32_t)mode);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_CREATE, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_mkdir(const char *path, mode_t mode)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_u32(req, &pos, (uint32_t)mode);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_MKDIR, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_unlink(const char *path)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_UNLINK, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_rmdir(const char *path)
{
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_RMDIR, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_rename(const char *from, const char *to, unsigned int flags)
{
    (void)flags;
    uint8_t req[8192];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), from, &pos) < 0) return -ENAMETOOLONG;
    if (encode_path(req, sizeof(req), to, &pos) < 0) return -ENAMETOOLONG;

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_RENAME, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_truncate(const char *path, off_t size,
                        struct fuse_file_info *fi)
{
    (void)fi;
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_i64(req, &pos, (int64_t)size);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_TRUNCATE, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_chmod(const char *path, mode_t mode,
                     struct fuse_file_info *fi)
{
    (void)fi;
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_u32(req, &pos, (uint32_t)mode);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_CHMOD, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

static int nfs_utimens(const char *path, const struct timespec ts[2],
                       struct fuse_file_info *fi)
{
    (void)fi;
    uint8_t req[4096];
    size_t pos = 0;
    if (encode_path(req, sizeof(req), path, &pos) < 0) return -ENAMETOOLONG;
    encode_i64(req, &pos, (int64_t)ts[0].tv_sec);
    encode_i64(req, &pos, (int64_t)ts[0].tv_nsec);
    encode_i64(req, &pos, (int64_t)ts[1].tv_sec);
    encode_i64(req, &pos, (int64_t)ts[1].tv_nsec);

    int32_t status; void *resp = NULL; uint32_t rlen = 0;
    int ret = do_rpc(OP_UTIMENS, req, (uint32_t)pos, &status, &resp, &rlen);
    free(resp);
    if (ret < 0) return ret;
    return status;
}

/* ── FUSE operations table ───────────────────────────────────────── */

static const struct fuse_operations nfs_oper = {
    .getattr  = nfs_getattr,
    .readdir  = nfs_readdir,
    .open     = nfs_open,
    .read     = nfs_read,
    .write    = nfs_write,
    .create   = nfs_create,
    .mkdir    = nfs_mkdir,
    .unlink   = nfs_unlink,
    .rmdir    = nfs_rmdir,
    .rename   = nfs_rename,
    .truncate = nfs_truncate,
    .chmod    = nfs_chmod,
    .utimens  = nfs_utimens,
};

/* ── Main ────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --host=<ip> --port=<port> <mountpoint> [fuse_opts]\n",
        prog);
}

int main(int argc, char *argv[])
{
    /* Extract --host and --port from argv before passing to fuse_main */
    int fuse_argc = 0;
    char **fuse_argv = malloc(sizeof(char *) * (argc + 1));
    if (!fuse_argv) { perror("malloc"); return 1; }

    fuse_argv[fuse_argc++] = argv[0];

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--host=", 7) == 0) {
            strncpy(g_host, argv[i] + 7, sizeof(g_host) - 1);
        } else if (strncmp(argv[i], "--port=", 7) == 0) {
            g_port = atoi(argv[i] + 7);
        } else {
            fuse_argv[fuse_argc++] = argv[i];
        }
    }
    fuse_argv[fuse_argc] = NULL;

    if (fuse_argc < 2) {
        usage(argv[0]);
        free(fuse_argv);
        return 1;
    }

    fprintf(stderr, "[client] Connecting to %s:%d\n", g_host, g_port);

    int ret = fuse_main(fuse_argc, fuse_argv, &nfs_oper, NULL);

    if (g_sockfd >= 0) close(g_sockfd);
    free(fuse_argv);
    return ret;
}
