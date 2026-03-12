#define _POSIX_C_SOURCE 200112L

#include "protocol.h"

#include <ctype.h>
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>

#define DEFAULT_PORT 8784

typedef struct {
    int sockfd;
    char mark;
    int running;
    int can_move;
    char board[BOARD_CELLS];
} ClientState;

static void reset_board(ClientState *state) {
    for (int i = 0; i < BOARD_CELLS; ++i) {
        state->board[i] = ' ';
    }
}

static void print_board(const ClientState *state) {
    printf("\n");
    for (int i = 0; i < BOARD_CELLS; ++i) {
        char cell = state->board[i];
        if (cell == ' ') {
            cell = (char)('1' + i);
        }
        printf(" %c ", cell);
        if (i % 3 != 2) {
            printf("|");
        }
        if (i % 3 == 2 && i != 8) {
            printf("\n---+---+---\n");
        }
    }
    printf("\n\n");
}

static void apply_board(ClientState *state, const char *wire_board) {
    for (int i = 0; i < BOARD_CELLS; ++i) {
        state->board[i] = (wire_board[i] == '_') ? ' ' : wire_board[i];
    }
}

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int sockfd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            continue;
        }
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);
    return sockfd;
}

static void print_help(void) {
    printf("Enter a move as a number from 1 to 9. Type 'quit' to leave.\n");
}

static const char *trim_leading(const char *text) {
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static void handle_server_message(ClientState *state, const char *message) {
    if (strncmp(message, "JOIN ", 5) == 0) {
        state->mark = message[5];
        printf("Connected as Player %c\n", state->mark);
    } else if (strncmp(message, "START ", 6) == 0) {
        printf("Game started. Player %c moves first.\n", message[6]);
        print_help();
    } else if (strncmp(message, "BOARD ", 6) == 0) {
        apply_board(state, message + 6);
        print_board(state);
    } else if (strcmp(message, MSG_YOUR_TURN) == 0) {
        state->can_move = 1;
        printf("Your turn. Choose a cell 1-9: ");
        fflush(stdout);
    } else if (strcmp(message, MSG_WAIT) == 0) {
        state->can_move = 0;
        printf("Waiting for the other player...\n");
    } else if (strcmp(message, MSG_OK) == 0) {
        printf("Move accepted.\n");
    } else if (strncmp(message, "ERR ", 4) == 0) {
        printf("Server error: %s\n", message + 4);
        if (state->can_move) {
            printf("Choose another cell 1-9: ");
            fflush(stdout);
        }
    } else if (strncmp(message, "WIN ", 4) == 0) {
        printf("Game over. Winner: %c\n", message[4]);
        state->running = 0;
    } else if (strcmp(message, MSG_DRAW) == 0) {
        printf("Game over. Draw.\n");
        state->running = 0;
    } else if (strncmp(message, "QUIT ", 5) == 0) {
        printf("Session ended: %s\n", message + 5);
        state->running = 0;
    } else if (strncmp(message, "INFO ", 5) == 0) {
        printf("%s\n", message + 5);
    } else {
        printf("Server says: %s\n", message);
    }
}

static int send_move(ClientState *state, const char *line) {
    line = trim_leading(line);

    if (strcmp(line, "quit") == 0) {
        return (send_frame(state->sockfd, MSG_QUIT) < 0) ? -1 : 1;
    }

    char *endptr = NULL;
    long move = strtol(line, &endptr, 10);
    while (endptr != NULL && *endptr != '\0' && isspace((unsigned char)*endptr)) {
        endptr++;
    }

    if (endptr == line || (endptr != NULL && *endptr != '\0')) {
        printf("Invalid input. Enter 1-9 or 'quit'.\n");
        return 0;
    }

    char message[MAX_FRAME_SIZE];
    snprintf(message, sizeof(message), "%s %ld", MSG_MOVE, move);
    return (send_frame(state->sockfd, message) < 0) ? -1 : 1;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <hostname-or-ip> [port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *host = argv[1];
    char port_buf[16];
    snprintf(port_buf, sizeof(port_buf), "%d", (argc == 3) ? atoi(argv[2]) : DEFAULT_PORT);

    int sockfd = connect_to_server(host, port_buf);
    if (sockfd < 0) {
        fprintf(stderr, "Unable to connect to %s:%s\n", host, port_buf);
        return EXIT_FAILURE;
    }

    ClientState state;
    memset(&state, 0, sizeof(state));
    state.sockfd = sockfd;
    state.running = 1;
    reset_board(&state);

    printf("Connected to %s:%s\n", host, port_buf);

    while (state.running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_fd = (sockfd > STDIN_FILENO) ? sockfd : STDIN_FILENO;

        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(sockfd, &readfds)) {
            char message[MAX_FRAME_SIZE];
            int rc = recv_frame(sockfd, message, sizeof(message));
            if (rc <= 0) {
                printf("Disconnected from server.\n");
                break;
            }
            handle_server_message(&state, message);
        }

        if (!state.running) {
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char line[64];
            if (fgets(line, sizeof(line), stdin) == NULL) {
                send_frame(sockfd, MSG_QUIT);
                break;
            }

            line[strcspn(line, "\r\n")] = '\0';
            const char *trimmed = trim_leading(line);

            if (!state.can_move && strcmp(trimmed, "quit") != 0) {
                printf("It is not your turn yet.\n");
                continue;
            }

            int send_rc = send_move(&state, trimmed);
            if (send_rc < 0) {
                perror("send_frame");
                break;
            }

            if (strcmp(trimmed, "quit") == 0) {
                break;
            }

            if (send_rc > 0) {
                state.can_move = 0;
            }
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
