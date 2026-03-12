#define _POSIX_C_SOURCE 200112L

#include "protocol.h"

#include <signal.h>
#include <netinet/in.h>
#include <sys/select.h>

#define DEFAULT_PORT 8784
#define BACKLOG 4

typedef struct {
    int fd;
    char mark;
} Player;

typedef struct {
    int listen_fd;
    Player players[2];
    char board[BOARD_CELLS];
    int current_turn;
    int move_count;
    int game_started;
} GameServer;

static void reset_board(GameServer *game) {
    for (int i = 0; i < BOARD_CELLS; ++i) {
        game->board[i] = ' ';
    }
    game->current_turn = 0;
    game->move_count = 0;
    game->game_started = 0;
}

static void send_to_player(const Player *player, const char *message) {
    if (player->fd >= 0) {
        if (send_frame(player->fd, message) < 0) {
            perror("send_frame");
        }
    }
}

static void broadcast(GameServer *game, const char *message) {
    for (int i = 0; i < 2; ++i) {
        send_to_player(&game->players[i], message);
    }
}

static void broadcast_board(GameServer *game) {
    char wire_board[BOARD_CELLS + 1];
    char message[MAX_FRAME_SIZE];

    board_to_wire(game->board, wire_board);
    snprintf(message, sizeof(message), "%s %s", MSG_BOARD, wire_board);
    broadcast(game, message);
}

static int check_winner(const char board[BOARD_CELLS]) {
    static const int wins[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6}
    };

    for (int i = 0; i < 8; ++i) {
        int a = wins[i][0];
        int b = wins[i][1];
        int c = wins[i][2];
        if (board[a] != ' ' && board[a] == board[b] && board[b] == board[c]) {
            return board[a];
        }
    }
    return 0;
}

static void send_turn_state(GameServer *game) {
    if (!game->game_started) {
        return;
    }

    send_to_player(&game->players[game->current_turn], MSG_YOUR_TURN);
    send_to_player(&game->players[1 - game->current_turn], MSG_WAIT);
}

static void start_game_if_ready(GameServer *game) {
    if (game->players[0].fd < 0 || game->players[1].fd < 0 || game->game_started) {
        return;
    }

    game->game_started = 1;
    send_to_player(&game->players[0], "START X");
    send_to_player(&game->players[1], "START O");
    broadcast_board(game);
    send_turn_state(game);
}

static void close_player(GameServer *game, int idx, const char *reason) {
    if (idx < 0 || idx > 1 || game->players[idx].fd < 0) {
        return;
    }

    int fd = game->players[idx].fd;
    char mark = game->players[idx].mark;
    int other = 1 - idx;

    if (reason != NULL) {
        send_to_player(&game->players[idx], reason);
    }

    close(fd);
    game->players[idx].fd = -1;

    if (game->players[other].fd >= 0) {
        char message[MAX_FRAME_SIZE];
        snprintf(message, sizeof(message), "%s player_%c_disconnected", MSG_QUIT, mark);
        send_to_player(&game->players[other], message);
        close(game->players[other].fd);
        game->players[other].fd = -1;
    }

    reset_board(game);
}

static void close_session(GameServer *game) {
    for (int i = 0; i < 2; ++i) {
        if (game->players[i].fd >= 0) {
            close(game->players[i].fd);
            game->players[i].fd = -1;
        }
    }
    reset_board(game);
}

static void handle_move(GameServer *game, int player_idx, const char *message) {
    char verb[16];
    int cell = -1;

    if (!game->game_started) {
        send_to_player(&game->players[player_idx], "ERR game_not_started");
        return;
    }

    if (player_idx != game->current_turn) {
        send_to_player(&game->players[player_idx], "ERR not_your_turn");
        return;
    }

    if (sscanf(message, "%15s %d", verb, &cell) != 2 || strcmp(verb, MSG_MOVE) != 0) {
        send_to_player(&game->players[player_idx], "ERR bad_move_format");
        return;
    }

    if (cell < 1 || cell > 9) {
        send_to_player(&game->players[player_idx], "ERR cell_out_of_range");
        return;
    }

    cell -= 1;
    if (game->board[cell] != ' ') {
        send_to_player(&game->players[player_idx], "ERR cell_occupied");
        return;
    }

    game->board[cell] = game->players[player_idx].mark;
    game->move_count++;
    send_to_player(&game->players[player_idx], MSG_OK);
    broadcast_board(game);

    int winner = check_winner(game->board);
    if (winner != 0) {
        char result[MAX_FRAME_SIZE];
        snprintf(result, sizeof(result), "%s %c", MSG_WIN, winner);
        broadcast(game, result);
        close_session(game);
        return;
    }

    if (game->move_count == BOARD_CELLS) {
        broadcast(game, MSG_DRAW);
        close_session(game);
        return;
    }

    game->current_turn = 1 - game->current_turn;
    send_turn_state(game);
}

static int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

static void announce_waiting(Player *player) {
    send_to_player(player, "INFO waiting_for_second_player");
}

static void accept_client(GameServer *game) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(game->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
        perror("accept");
        return;
    }

    int slot = (game->players[0].fd < 0) ? 0 : ((game->players[1].fd < 0) ? 1 : -1);
    if (slot < 0) {
        send_frame(client_fd, "ERR server_full");
        close(client_fd);
        return;
    }

    game->players[slot].fd = client_fd;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("Client connected: %s:%d as %c\n",
           client_ip,
           ntohs(client_addr.sin_port),
           game->players[slot].mark);

    char joined[MAX_FRAME_SIZE];
    snprintf(joined, sizeof(joined), "%s %c", MSG_JOIN, game->players[slot].mark);
    send_to_player(&game->players[slot], joined);

    if (slot == 0) {
        announce_waiting(&game->players[slot]);
    } else {
        send_to_player(&game->players[slot], "INFO game_will_start");
    }

    start_game_if_ready(game);
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    GameServer game;
    memset(&game, 0, sizeof(game));
    game.players[0].fd = -1;
    game.players[0].mark = 'X';
    game.players[1].fd = -1;
    game.players[1].mark = 'O';
    reset_board(&game);

    game.listen_fd = create_server_socket(port);
    if (game.listen_fd < 0) {
        return EXIT_FAILURE;
    }

    printf("Tic-Tac-Toe server listening on port %d\n", port);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(game.listen_fd, &readfds);
        int max_fd = game.listen_fd;

        for (int i = 0; i < 2; ++i) {
            if (game.players[i].fd >= 0) {
                FD_SET(game.players[i].fd, &readfds);
                if (game.players[i].fd > max_fd) {
                    max_fd = game.players[i].fd;
                }
            }
        }

        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(game.listen_fd, &readfds)) {
            accept_client(&game);
        }

        for (int i = 0; i < 2; ++i) {
            if (game.players[i].fd < 0 || !FD_ISSET(game.players[i].fd, &readfds)) {
                continue;
            }

            char message[MAX_FRAME_SIZE];
            int rc = recv_frame(game.players[i].fd, message, sizeof(message));
            if (rc <= 0) {
                printf("Player %c disconnected\n", game.players[i].mark);
                close_player(&game, i, NULL);
                break;
            }

            if (strncmp(message, MSG_MOVE, strlen(MSG_MOVE)) == 0) {
                handle_move(&game, i, message);
            } else if (strcmp(message, MSG_QUIT) == 0) {
                close_player(&game, i, NULL);
                break;
            } else {
                send_to_player(&game.players[i], "ERR unknown_command");
            }
        }
    }

    close(game.listen_fd);
    close_player(&game, 0, NULL);
    close_player(&game, 1, NULL);
    return EXIT_SUCCESS;
}
