/*
 * server.c — NetFS server main entry point
 *
 * Usage: ./netfs_server <port> <backing_directory>
 */

#include "../common/protocol.h"
#include "handlers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void sigterm_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void serve_client(int client_fd, const char *base_dir)
{
    fprintf(stderr, "[server] Client connected (fd=%d)\n", client_fd);
    while (1) {
        uint8_t opcode;
        void *payload = NULL;
        uint32_t payload_len = 0;

        if (netfs_recv_request(client_fd, &opcode,
                               &payload, &payload_len) < 0) {
            fprintf(stderr, "[server] Client disconnected\n");
            break;
        }

        switch (opcode) {
        case OP_GETATTR:  handle_getattr(client_fd, base_dir, payload, payload_len); break;
        case OP_READDIR:  handle_readdir(client_fd, base_dir, payload, payload_len); break;
        case OP_OPEN:     handle_open(client_fd, base_dir, payload, payload_len);    break;
        case OP_READ:     handle_read(client_fd, base_dir, payload, payload_len);    break;
        case OP_WRITE:    handle_write(client_fd, base_dir, payload, payload_len);   break;
        case OP_CREATE:   handle_create(client_fd, base_dir, payload, payload_len);  break;
        case OP_MKDIR:    handle_mkdir(client_fd, base_dir, payload, payload_len);   break;
        case OP_UNLINK:   handle_unlink(client_fd, base_dir, payload, payload_len);  break;
        case OP_RMDIR:    handle_rmdir(client_fd, base_dir, payload, payload_len);   break;
        case OP_RENAME:   handle_rename(client_fd, base_dir, payload, payload_len);  break;
        case OP_TRUNCATE: handle_truncate(client_fd, base_dir, payload, payload_len);break;
        case OP_CHMOD:    handle_chmod(client_fd, base_dir, payload, payload_len);   break;
        case OP_UTIMENS:  handle_utimens(client_fd, base_dir, payload, payload_len); break;
        default:
            fprintf(stderr, "[server] Unknown opcode: %d\n", opcode);
            netfs_send_response(client_fd, -ENOSYS, NULL, 0);
            break;
        }
        free(payload);
    }
    close(client_fd);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <backing_directory>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    const char *base_dir = argv[2];

    struct stat st;
    if (stat(base_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[server] '%s' is not a valid directory\n", base_dir);
        return 1;
    }

    /* Signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Create socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); close(server_fd); return 1;
    }

    fprintf(stderr, "[server] Listening on port %d, dir: %s\n",
            port, base_dir);

    while (g_running) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int cfd = accept(server_fd, (struct sockaddr *)&ca, &cl);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
        fprintf(stderr, "[server] Connection from %s:%d\n",
                ip, ntohs(ca.sin_port));

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(cfd); continue; }
        if (pid == 0) { close(server_fd); serve_client(cfd, base_dir); exit(0); }
        close(cfd);
    }

    fprintf(stderr, "[server] Shutting down\n");
    close(server_fd);
    return 0;
}
