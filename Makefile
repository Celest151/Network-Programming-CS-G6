CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c11

TARGETS = server

all: $(TARGETS)

server: server.c
	$(CC) $(CFLAGS) -o $@ server.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
