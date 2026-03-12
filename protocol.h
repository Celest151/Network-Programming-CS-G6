#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BOARD_CELLS 9
#define MAX_FRAME_SIZE 256

#define MSG_JOIN "JOIN"
#define MSG_START "START"
#define MSG_BOARD "BOARD"
#define MSG_YOUR_TURN "YOUR_TURN"
#define MSG_WAIT "WAIT"
#define MSG_MOVE "MOVE"
#define MSG_OK "OK"
#define MSG_ERR "ERR"
#define MSG_WIN "WIN"
#define MSG_DRAW "DRAW"
#define MSG_QUIT "QUIT"
#define MSG_INFO "INFO"

static inline int send_all(int fd, const void *buf, size_t len) {
    const char *ptr = (const char *)buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t rc = send(fd, ptr + sent, len - sent, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        sent += (size_t)rc;
    }

    return 0;
}

static inline int recv_all(int fd, void *buf, size_t len) {
    char *ptr = (char *)buf;
    size_t received = 0;

    while (received < len) {
        ssize_t rc = recv(fd, ptr + received, len - received, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return 0;
        }
        received += (size_t)rc;
    }

    return 1;
}

static inline int send_frame(int fd, const char *message) {
    uint32_t len = (uint32_t)strlen(message);
    uint32_t net_len = htonl(len);

    if (send_all(fd, &net_len, sizeof(net_len)) < 0) {
        return -1;
    }
    if (send_all(fd, message, len) < 0) {
        return -1;
    }
    return 0;
}

static inline int recv_frame(int fd, char *buffer, size_t buffer_size) {
    uint32_t net_len = 0;
    int rc = recv_all(fd, &net_len, sizeof(net_len));
    if (rc <= 0) {
        return rc;
    }

    uint32_t len = ntohl(net_len);
    if (len >= buffer_size || len >= MAX_FRAME_SIZE) {
        errno = EMSGSIZE;
        return -1;
    }

    rc = recv_all(fd, buffer, len);
    if (rc <= 0) {
        return rc;
    }

    buffer[len] = '\0';
    return 1;
}

static inline void board_to_wire(const char board[BOARD_CELLS], char out[BOARD_CELLS + 1]) {
    for (int i = 0; i < BOARD_CELLS; ++i) {
        out[i] = (board[i] == ' ') ? '_' : board[i];
    }
    out[BOARD_CELLS] = '\0';
}

#endif
