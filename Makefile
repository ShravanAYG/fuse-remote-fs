# NetFS Makefile

CC       = gcc
CFLAGS   = -Wall -Wextra -g $(shell pkg-config --cflags fuse3)
LDFLAGS_FUSE = $(shell pkg-config --libs fuse3)

COMMON_SRC = common/protocol.c
COMMON_OBJ = common/protocol.o

SERVER_SRC = server/server.c server/handlers.c
SERVER_OBJ = server/server.o server/handlers.o

CLIENT_SRC = client/client.c
CLIENT_OBJ = client/client.o

API_SRC = api/netfs_api.c
API_OBJ = api/netfs_api.o

EXAMPLE_SRC = examples/api_usage.c
EXAMPLE_OBJ = examples/api_usage.o

.PHONY: all clean

all: netfs_server netfs_client netfs_example

netfs_server: $(SERVER_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

netfs_client: $(CLIENT_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_FUSE)

netfs_example: $(EXAMPLE_OBJ) $(API_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Object rules
common/protocol.o: common/protocol.c common/protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

server/server.o: server/server.c server/handlers.h common/protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

server/handlers.o: server/handlers.c server/handlers.h common/protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

client/client.o: client/client.c common/protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

api/netfs_api.o: api/netfs_api.c api/netfs_api.h common/protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

examples/api_usage.o: examples/api_usage.c api/netfs_api.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f netfs_server netfs_client netfs_example
	rm -f common/*.o server/*.o client/*.o api/*.o examples/*.o
