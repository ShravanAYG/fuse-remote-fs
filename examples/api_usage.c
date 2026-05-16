/*
 * example.c — Demonstrates the NetFS C API
 *
 * Build:  make example
 * Run:    ./netfs_example        (assumes server on 127.0.0.1:9000)
 */

#include "../api/netfs_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *host = "127.0.0.1";
    int port = 9000;

    if (argc >= 3) {
        host = argv[1];
        port = atoi(argv[2]);
    }

    /* Connect */
    printf("Connecting to %s:%d ...\n", host, port);
    netfs_ctx *ctx = netfs_connect(host, port);
    if (!ctx) {
        fprintf(stderr, "Failed to connect!\n");
        return 1;
    }
    printf("Connected.\n\n");

    /* Create a file and write content */
    printf("Creating /demo.txt ...\n");
    int ret = netfs_create_file(ctx, "/demo.txt", 0644);
    printf("  create: %s\n\n", ret == 0 ? "OK" : "FAIL");

    const char *msg = "Hello from the NetFS C API!";
    printf("Writing: \"%s\"\n", msg);
    ret = netfs_write_file(ctx, "/demo.txt", msg, strlen(msg), 0);
    printf("  write: %d bytes\n\n", ret);

    /* Read it back into a char buffer */
    char buf[256] = {0};
    int n = netfs_read_file(ctx, "/demo.txt", buf, sizeof(buf) - 1, 0);
    printf("Read %d bytes: \"%s\"\n\n", n, buf);

    /* Stat the file */
    struct stat st;
    ret = netfs_stat(ctx, "/demo.txt", &st);
    if (ret == 0) {
        printf("Stat /demo.txt:\n");
        printf("  size  = %ld bytes\n", (long)st.st_size);
        printf("  mode  = %o\n", st.st_mode & 0777);
    }
    printf("\n");

    /* Create a directory */
    printf("Creating /api_test_dir ...\n");
    ret = netfs_mkdir(ctx, "/api_test_dir", 0755);
    printf("  mkdir: %s\n\n", ret == 0 ? "OK" : "FAIL");

    /* List the root directory */
    char *entries[64];
    int count = netfs_list_dir(ctx, "/", entries, 64);
    printf("Root directory (%d entries):\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %s\n", entries[i]);
        free(entries[i]);
    }
    printf("\n");

    /* Rename the file */
    printf("Renaming /demo.txt -> /demo_renamed.txt ...\n");
    ret = netfs_rename(ctx, "/demo.txt", "/demo_renamed.txt");
    printf("  rename: %s\n\n", ret == 0 ? "OK" : "FAIL");

    /* Read from the renamed file */
    memset(buf, 0, sizeof(buf));
    n = netfs_read_file(ctx, "/demo_renamed.txt", buf, sizeof(buf) - 1, 0);
    printf("Read renamed file (%d bytes): \"%s\"\n\n", n, buf);

    /* Cleanup: delete file and dir */
    printf("Cleaning up...\n");
    netfs_delete_file(ctx, "/demo_renamed.txt");
    netfs_rmdir(ctx, "/api_test_dir");
    printf("  Done.\n\n");

    /* Disconnect */
    netfs_disconnect(ctx);
    printf("Disconnected.\n");
    return 0;
}
