CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c11

TARGETS = server client

all: $(TARGETS)

server: server.c protocol.h
	$(CC) $(CFLAGS) -o $@ server.c

client: client.c protocol.h
	$(CC) $(CFLAGS) -o $@ client.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
