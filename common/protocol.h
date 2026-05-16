/*
 * protocol.h - Wire protocol for NetFS
 */

#ifndef NETFS_PROTOCOL_H
#define NETFS_PROTOCOL_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>



enum netfs_opcode {
    OP_GETATTR  = 1,
    OP_READDIR  = 2,
    OP_OPEN     = 3,
    OP_READ     = 4,
    OP_WRITE    = 5,
    OP_CREATE   = 6,
    OP_MKDIR    = 7,
    OP_UNLINK   = 8,
    OP_RMDIR    = 9,
    OP_RENAME   = 10,
    OP_TRUNCATE = 11,
    OP_CHMOD    = 12,
    OP_UTIMENS  = 13,
};



// Request: [opcode:1][len:4]
typedef struct {
    uint8_t  opcode;
    uint32_t payload_len;
} __attribute__((packed)) netfs_req_header_t;

// Response: [status:4][len:4]
typedef struct {
    int32_t  status;
    uint32_t payload_len;
} __attribute__((packed)) netfs_resp_header_t;



// fixed-layout struct for wire (arch independent)
typedef struct {
    uint32_t mode;       /* File type and permissions */
    uint32_t nlink;      /* Number of hard links */
    uint32_t uid;
    uint32_t gid;
    int64_t  size;       /* Total size in bytes */
    int64_t  atime_sec;
    int64_t  mtime_sec;
    int64_t  ctime_sec;
    int64_t  atime_nsec;
    int64_t  mtime_nsec;
    int64_t  ctime_nsec;
    uint64_t blocks;     /* Number of 512B blocks allocated */
    uint32_t blksize;    /* Block size for filesystem I/O */
} __attribute__((packed)) netfs_stat_t;



// IO helpers
int netfs_read_full(int fd, void *buf, size_t len);
int netfs_write_full(int fd, const void *buf, size_t len);

/*
 * Send a request header + payload.
 * Returns 0 on success, -1 on failure.
 */
int netfs_send_request(int fd, uint8_t opcode,
                       const void *payload, uint32_t payload_len);

/*
 * Receive a request header + payload.
 * Allocates *payload_out (caller must free).
 * Returns 0 on success, -1 on failure.
 */
int netfs_recv_request(int fd, uint8_t *opcode_out,
                       void **payload_out, uint32_t *payload_len_out);

/*
 * Send a response header + payload.
 * Returns 0 on success, -1 on failure.
 */
int netfs_send_response(int fd, int32_t status,
                        const void *payload, uint32_t payload_len);

/*
 * Receive a response header + payload.
 * Allocates *payload_out (caller must free) if payload_len > 0.
 * Returns 0 on success, -1 on failure.
 */
int netfs_recv_response(int fd, int32_t *status_out,
                        void **payload_out, uint32_t *payload_len_out);



void netfs_stat_to_wire(const struct stat *st, netfs_stat_t *wire);
void netfs_stat_from_wire(const netfs_stat_t *wire, struct stat *st);

#endif /* NETFS_PROTOCOL_H */
