/*
 * handlers.h - Server operation handlers
 */

#ifndef NETFS_HANDLERS_H
#define NETFS_HANDLERS_H

#include <stdint.h>



void handle_getattr(int client_fd, const char *base_dir,
                    const void *payload, uint32_t payload_len);

void handle_readdir(int client_fd, const char *base_dir,
                    const void *payload, uint32_t payload_len);

void handle_open(int client_fd, const char *base_dir,
                 const void *payload, uint32_t payload_len);

void handle_read(int client_fd, const char *base_dir,
                 const void *payload, uint32_t payload_len);

void handle_write(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len);

void handle_create(int client_fd, const char *base_dir,
                   const void *payload, uint32_t payload_len);

void handle_mkdir(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len);

void handle_unlink(int client_fd, const char *base_dir,
                   const void *payload, uint32_t payload_len);

void handle_rmdir(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len);

void handle_rename(int client_fd, const char *base_dir,
                   const void *payload, uint32_t payload_len);

void handle_truncate(int client_fd, const char *base_dir,
                     const void *payload, uint32_t payload_len);

void handle_chmod(int client_fd, const char *base_dir,
                  const void *payload, uint32_t payload_len);

void handle_utimens(int client_fd, const char *base_dir,
                    const void *payload, uint32_t payload_len);

#endif /* NETFS_HANDLERS_H */
