#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 8784
#define BACKLOG 8
#define BOARD_CELLS 9
#define REQUEST_BUF_SIZE 8192
#define TOKEN_SIZE 32
#define PLAYER_TIMEOUT 20

typedef struct {
    int active;
    char mark;
    char token[TOKEN_SIZE];
    time_t last_seen;
} Player;

typedef struct {
    char board[BOARD_CELLS];
    Player players[2];
    int current_turn;
    int move_count;
    int game_started;
    int winner;
    int draw;
    char status[128];
    unsigned int token_seed;
} GameState;

typedef struct {
    char method[8];
    char path[256];
    char query[256];
    char content_type[64];
    char body[2048];
    int content_length;
} HttpRequest;

static void reset_board(GameState *game) {
    for (int i = 0; i < BOARD_CELLS; ++i) {
        game->board[i] = ' ';
    }
    game->current_turn = 0;
    game->move_count = 0;
    game->game_started = 0;
    game->winner = 0;
    game->draw = 0;
}

static void set_status(GameState *game, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(game->status, sizeof(game->status), fmt, args);
    va_end(args);
}

static void init_game(GameState *game) {
    memset(game, 0, sizeof(*game));
    game->players[0].mark = 'X';
    game->players[1].mark = 'O';
    game->token_seed = 1U;
    reset_board(game);
    set_status(game, "Waiting for Player X and Player O.");
}

static int send_all(int fd, const void *buf, size_t len) {
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

static int check_winner(const char board[BOARD_CELLS]) {
    static const int wins[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6}
    };

    for (int i = 0; i < 8; ++i) {
        const int a = wins[i][0];
        const int b = wins[i][1];
        const int c = wins[i][2];
        if (board[a] != ' ' && board[a] == board[b] && board[b] == board[c]) {
            return board[a];
        }
    }

    return 0;
}

static void board_to_wire(const GameState *game, char out[BOARD_CELLS + 1]) {
    for (int i = 0; i < BOARD_CELLS; ++i) {
        out[i] = (game->board[i] == ' ') ? '_' : game->board[i];
    }
    out[BOARD_CELLS] = '\0';
}

static void generate_token(GameState *game, int slot) {
    snprintf(game->players[slot].token,
             sizeof(game->players[slot].token),
             "%c-%u-%ld",
             game->players[slot].mark,
             game->token_seed++,
             (long)time(NULL));
}

static int find_player_by_token(GameState *game, const char *token) {
    if (token == NULL || token[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < 2; ++i) {
        if (game->players[i].active && strcmp(game->players[i].token, token) == 0) {
            return i;
        }
    }

    return -1;
}

static void clear_player(GameState *game, int slot) {
    if (slot < 0 || slot > 1) {
        return;
    }

    game->players[slot].active = 0;
    game->players[slot].token[0] = '\0';
    game->players[slot].last_seen = 0;
}

static void reset_after_disconnect(GameState *game, char missing_mark) {
    reset_board(game);
    if (game->players[0].active && game->players[1].active) {
        set_status(game, "Both players connected. Game ready.");
    } else if (game->players[0].active || game->players[1].active) {
        set_status(game, "Player %c left. Waiting for another player.", missing_mark);
    } else {
        set_status(game, "Waiting for Player X and Player O.");
    }
}

static void maybe_start_game(GameState *game) {
    if (game->players[0].active && game->players[1].active) {
        reset_board(game);
        game->game_started = 1;
        set_status(game, "Game started. Player X moves first.");
    } else if (game->players[0].active || game->players[1].active) {
        set_status(game, "Waiting for a second player.");
    } else {
        set_status(game, "Waiting for Player X and Player O.");
    }
}

static void cleanup_inactive_players(GameState *game) {
    time_t now = time(NULL);

    for (int i = 0; i < 2; ++i) {
        if (!game->players[i].active) {
            continue;
        }
        if ((now - game->players[i].last_seen) > PLAYER_TIMEOUT) {
            char mark = game->players[i].mark;
            clear_player(game, i);
            reset_after_disconnect(game, mark);
        }
    }
}

static int assign_player(GameState *game, const char *token) {
    int existing = find_player_by_token(game, token);
    if (existing >= 0) {
        game->players[existing].last_seen = time(NULL);
        return existing;
    }

    for (int i = 0; i < 2; ++i) {
        if (!game->players[i].active) {
            game->players[i].active = 1;
            generate_token(game, i);
            game->players[i].last_seen = time(NULL);
            maybe_start_game(game);
            return i;
        }
    }

    return -1;
}

static void leave_player(GameState *game, int slot) {
    if (slot < 0) {
        return;
    }

    char mark = game->players[slot].mark;
    clear_player(game, slot);
    reset_after_disconnect(game, mark);
}

static const char *query_param(const char *source, const char *key, char *out, size_t out_size) {
    const char *cursor = source;
    size_t key_len = strlen(key);

    if (out_size == 0) {
        return NULL;
    }
    out[0] = '\0';

    while (cursor != NULL && *cursor != '\0') {
        const char *eq = strchr(cursor, '=');
        const char *amp = strchr(cursor, '&');
        size_t name_len = 0;

        if (eq == NULL) {
            break;
        }
        name_len = (size_t)(eq - cursor);
        if (name_len == key_len && strncmp(cursor, key, key_len) == 0) {
            const char *value = eq + 1;
            size_t value_len = (amp == NULL) ? strlen(value) : (size_t)(amp - value);
            if (value_len >= out_size) {
                value_len = out_size - 1;
            }
            memcpy(out, value, value_len);
            out[value_len] = '\0';
            return out;
        }

        cursor = (amp == NULL) ? NULL : (amp + 1);
    }

    return NULL;
}

static int read_http_request(int client_fd, HttpRequest *request) {
    char buffer[REQUEST_BUF_SIZE];
    size_t total = 0;
    ssize_t rc;
    char *header_end = NULL;

    memset(request, 0, sizeof(*request));
    buffer[0] = '\0';

    while ((header_end = strstr(buffer, "\r\n\r\n")) == NULL) {
        rc = recv(client_fd, buffer + total, sizeof(buffer) - 1 - total, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        total += (size_t)rc;
        buffer[total] = '\0';
        if (total >= sizeof(buffer) - 1) {
            return -1;
        }
    }

    size_t header_len = (size_t)(header_end - buffer) + 4;
    char *line_end = strstr(buffer, "\r\n");
    if (line_end == NULL) {
        return -1;
    }
    *line_end = '\0';

    char target[256];
    if (sscanf(buffer, "%7s %255s", request->method, target) != 2) {
        return -1;
    }

    char *query_start = strchr(target, '?');
    if (query_start != NULL) {
        *query_start = '\0';
        snprintf(request->query, sizeof(request->query), "%s", query_start + 1);
    }
    snprintf(request->path, sizeof(request->path), "%s", target);

    request->content_length = 0;
    request->content_type[0] = '\0';

    char *headers = line_end + 2;
    char *cursor = headers;
    while (cursor < header_end) {
        char *next = strstr(cursor, "\r\n");
        if (next == NULL || next == cursor) {
            break;
        }
        *next = '\0';

        if (strncasecmp(cursor, "Content-Length:", 15) == 0) {
            request->content_length = atoi(cursor + 15);
        } else if (strncasecmp(cursor, "Content-Type:", 13) == 0) {
            const char *value = cursor + 13;
            while (*value == ' ') {
                value++;
            }
            snprintf(request->content_type, sizeof(request->content_type), "%s", value);
        }

        cursor = next + 2;
    }

    if (request->content_length > 0) {
        size_t body_bytes = total - header_len;
        while (body_bytes < (size_t)request->content_length) {
            rc = recv(client_fd,
                      buffer + total,
                      sizeof(buffer) - 1 - total,
                      0);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
            if (rc == 0) {
                return -1;
            }
            total += (size_t)rc;
            body_bytes += (size_t)rc;
            buffer[total] = '\0';
        }

        size_t copy_len = (size_t)request->content_length;
        if (copy_len >= sizeof(request->body)) {
            copy_len = sizeof(request->body) - 1;
        }
        memcpy(request->body, buffer + header_len, copy_len);
        request->body[copy_len] = '\0';
    }

    return 0;
}

static int respond(int client_fd,
                   const char *status,
                   const char *content_type,
                   const char *body) {
    char header[512];
    size_t body_len = strlen(body);

    snprintf(header,
             sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Cache-Control: no-store\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             status,
             content_type,
             body_len);

    if (send_all(client_fd, header, strlen(header)) < 0) {
        return -1;
    }
    if (send_all(client_fd, body, body_len) < 0) {
        return -1;
    }
    return 0;
}

static int respond_json(int client_fd, const char *status, const char *json_body) {
    return respond(client_fd, status, "application/json; charset=utf-8", json_body);
}

static int respond_text(int client_fd, const char *status, const char *text_body) {
    return respond(client_fd, status, "text/plain; charset=utf-8", text_body);
}

static int read_file(const char *path, char **content, size_t *length) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);

    *content = (char *)malloc((size_t)size + 1);
    if (*content == NULL) {
        fclose(fp);
        return -1;
    }

    if (fread(*content, 1, (size_t)size, fp) != (size_t)size) {
        free(*content);
        fclose(fp);
        return -1;
    }

    (*content)[size] = '\0';
    *length = (size_t)size;
    fclose(fp);
    return 0;
}

static int respond_file(int client_fd, const char *path, const char *content_type) {
    char *content = NULL;
    size_t length = 0;
    char header[512];

    if (read_file(path, &content, &length) < 0) {
        return respond_text(client_fd, "404 Not Found", "Not found");
    }

    snprintf(header,
             sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Cache-Control: no-store\r\n"
             "Connection: close\r\n"
             "\r\n",
             content_type,
             length);

    if (send_all(client_fd, header, strlen(header)) < 0 ||
        send_all(client_fd, content, length) < 0) {
        free(content);
        return -1;
    }

    free(content);
    return 0;
}

static void write_state_json(GameState *game, int player_index, char *out, size_t out_size) {
    char board[BOARD_CELLS + 1];
    const char *role = "spectator";
    char your_mark = '-';
    int can_move = 0;

    board_to_wire(game, board);

    if (player_index >= 0) {
        role = "player";
        your_mark = game->players[player_index].mark;
        if (game->game_started &&
            !game->winner &&
            !game->draw &&
            game->current_turn == player_index) {
            can_move = 1;
        }
    }

    snprintf(out,
             out_size,
             "{"
             "\"board\":\"%s\","
             "\"gameStarted\":%s,"
             "\"winner\":\"%c\","
             "\"draw\":%s,"
             "\"currentTurn\":\"%c\","
             "\"status\":\"%s\","
             "\"role\":\"%s\","
             "\"yourMark\":\"%c\","
             "\"canMove\":%s,"
             "\"playersConnected\":%d"
             "}",
             board,
             game->game_started ? "true" : "false",
             game->winner ? game->winner : '-',
             game->draw ? "true" : "false",
             game->players[game->current_turn].mark,
             game->status,
             role,
             your_mark,
             can_move ? "true" : "false",
             game->players[0].active + game->players[1].active);
}

static void handle_join(GameState *game, int client_fd, const HttpRequest *request) {
    char token[TOKEN_SIZE];
    char body[512];
    query_param(request->body, "token", token, sizeof(token));

    int slot = assign_player(game, token);
    if (slot < 0) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"server_full\",\"message\":\"Two players are already in the game.\"}");
        return;
    }

    snprintf(body,
             sizeof(body),
             "{"
             "\"ok\":true,"
             "\"token\":\"%s\","
             "\"mark\":\"%c\","
             "\"message\":\"Joined as Player %c.\""
             "}",
             game->players[slot].token,
             game->players[slot].mark,
             game->players[slot].mark);
    respond_json(client_fd, "200 OK", body);
}

static void handle_state(GameState *game, int client_fd, const HttpRequest *request) {
    char token[TOKEN_SIZE];
    char body[512];
    query_param(request->query, "player", token, sizeof(token));

    int player_index = find_player_by_token(game, token);
    if (player_index >= 0) {
        game->players[player_index].last_seen = time(NULL);
    }

    write_state_json(game, player_index, body, sizeof(body));
    respond_json(client_fd, "200 OK", body);
}

static void handle_move(GameState *game, int client_fd, const HttpRequest *request) {
    char token[TOKEN_SIZE];
    char cell_value[16];
    char body[512];
    int slot;
    long cell;

    query_param(request->body, "player", token, sizeof(token));
    query_param(request->body, "cell", cell_value, sizeof(cell_value));

    slot = find_player_by_token(game, token);
    if (slot < 0) {
        respond_json(client_fd,
                     "403 Forbidden",
                     "{\"ok\":false,\"error\":\"invalid_player\",\"message\":\"Join the game before moving.\"}");
        return;
    }

    game->players[slot].last_seen = time(NULL);

    if (!game->game_started) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"game_not_started\",\"message\":\"Waiting for the second player.\"}");
        return;
    }

    if (game->winner || game->draw) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"game_finished\",\"message\":\"This round is already finished.\"}");
        return;
    }

    if (slot != game->current_turn) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"not_your_turn\",\"message\":\"Wait for your turn.\"}");
        return;
    }

    cell = strtol(cell_value, NULL, 10);
    if (cell < 1 || cell > 9) {
        respond_json(client_fd,
                     "400 Bad Request",
                     "{\"ok\":false,\"error\":\"bad_cell\",\"message\":\"Cell must be between 1 and 9.\"}");
        return;
    }

    if (game->board[cell - 1] != ' ') {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"occupied\",\"message\":\"That cell is already taken.\"}");
        return;
    }

    game->board[cell - 1] = game->players[slot].mark;
    game->move_count++;
    game->winner = check_winner(game->board);

    if (game->winner) {
        game->game_started = 0;
        set_status(game, "Player %c wins the round.", game->winner);
    } else if (game->move_count == BOARD_CELLS) {
        game->draw = 1;
        game->game_started = 0;
        set_status(game, "The round ended in a draw.");
    } else {
        game->current_turn = 1 - game->current_turn;
        set_status(game, "Player %c to move.", game->players[game->current_turn].mark);
    }

    snprintf(body,
             sizeof(body),
             "{"
             "\"ok\":true,"
             "\"message\":\"Move accepted.\""
             "}");
    respond_json(client_fd, "200 OK", body);
}

static void handle_leave(GameState *game, int client_fd, const HttpRequest *request) {
    char token[TOKEN_SIZE];
    query_param(request->body, "player", token, sizeof(token));

    int slot = find_player_by_token(game, token);
    if (slot >= 0) {
        leave_player(game, slot);
    }

    respond_json(client_fd,
                 "200 OK",
                 "{\"ok\":true,\"message\":\"You left the game.\"}");
}

static void handle_request(GameState *game, int client_fd, const HttpRequest *request) {
    if (strcmp(request->method, "GET") == 0 && strcmp(request->path, "/") == 0) {
        respond_file(client_fd, "web/index.html", "text/html; charset=utf-8");
        return;
    }

    if (strcmp(request->method, "GET") == 0 && strcmp(request->path, "/app.js") == 0) {
        respond_file(client_fd, "web/app.js", "application/javascript; charset=utf-8");
        return;
    }

    if (strcmp(request->method, "GET") == 0 && strcmp(request->path, "/styles.css") == 0) {
        respond_file(client_fd, "web/styles.css", "text/css; charset=utf-8");
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->path, "/api/join") == 0) {
        handle_join(game, client_fd, request);
        return;
    }

    if (strcmp(request->method, "GET") == 0 && strcmp(request->path, "/api/state") == 0) {
        handle_state(game, client_fd, request);
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->path, "/api/move") == 0) {
        handle_move(game, client_fd, request);
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->path, "/api/leave") == 0) {
        handle_leave(game, client_fd, request);
        return;
    }

    respond_text(client_fd, "404 Not Found", "Not found");
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

    GameState game;
    init_game(&game);

    int listen_fd = create_server_socket(port);
    if (listen_fd < 0) {
        return EXIT_FAILURE;
    }

    printf("Web Tic-Tac-Toe server listening on http://0.0.0.0:%d\n", port);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(listen_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        cleanup_inactive_players(&game);

        if (ready == 0 || !FD_ISSET(listen_fd, &readfds)) {
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        HttpRequest request;
        if (read_http_request(client_fd, &request) == 0) {
            handle_request(&game, client_fd, &request);
        } else {
            respond_text(client_fd, "400 Bad Request", "Bad request");
        }

        close(client_fd);
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
