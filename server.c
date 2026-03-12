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
#define MAX_BOARD_SIZE 8
#define MAX_BOARD_CELLS (MAX_BOARD_SIZE * MAX_BOARD_SIZE)
#define REQUEST_BUF_SIZE 8192
#define TOKEN_SIZE 40
#define ROOM_CODE_SIZE 16
#define PLAYER_TIMEOUT 20
#define MAX_ROOMS 64

typedef struct {
    int active;
    char mark;
    char token[TOKEN_SIZE];
    time_t last_seen;
} Player;

typedef struct {
    int in_use;
    char code[ROOM_CODE_SIZE];
    int board_size;
    char board[MAX_BOARD_CELLS];
    Player players[2];
    int current_turn;
    int move_count;
    int game_started;
    int winner;
    int draw;
    char status[128];
} Room;

typedef struct {
    Room rooms[MAX_ROOMS];
    unsigned int token_seed;
} ServerState;

typedef struct {
    char method[8];
    char path[256];
    char query[512];
    char content_type[64];
    char body[2048];
    int content_length;
} HttpRequest;

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

static int check_line(const char *board, int board_size, int row, int col, int row_step, int col_step) {
    char first = board[(row * board_size) + col];
    if (first == ' ') {
        return 0;
    }

    for (int step = 1; step < board_size; ++step) {
        int next_row = row + (row_step * step);
        int next_col = col + (col_step * step);
        if (board[(next_row * board_size) + next_col] != first) {
            return 0;
        }
    }

    return first;
}

static int check_winner(const char *board, int board_size) {
    for (int row = 0; row < board_size; ++row) {
        int row_win = check_line(board, board_size, row, 0, 0, 1);
        if (row_win) {
            return row_win;
        }
    }

    for (int col = 0; col < board_size; ++col) {
        int col_win = check_line(board, board_size, 0, col, 1, 0);
        if (col_win) {
            return col_win;
        }
    }

    int diag_win = check_line(board, board_size, 0, 0, 1, 1);
    if (diag_win) {
        return diag_win;
    }

    return check_line(board, board_size, 0, board_size - 1, 1, -1);
}

static void set_status(Room *room, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(room->status, sizeof(room->status), fmt, args);
    va_end(args);
}

static void reset_board(Room *room) {
    int cells = room->board_size * room->board_size;
    for (int i = 0; i < cells; ++i) {
        room->board[i] = ' ';
    }
    room->current_turn = 0;
    room->move_count = 0;
    room->game_started = 0;
    room->winner = 0;
    room->draw = 0;
}

static void init_room(Room *room, const char *code, int board_size) {
    memset(room, 0, sizeof(*room));
    room->in_use = 1;
    room->board_size = board_size;
    snprintf(room->code, sizeof(room->code), "%s", code);
    room->players[0].mark = 'X';
    room->players[1].mark = 'O';
    reset_board(room);
    set_status(room, "Room %s is waiting for players.", room->code);
}

static void init_server(ServerState *server) {
    memset(server, 0, sizeof(*server));
    server->token_seed = 1U;
}

static void board_to_wire(const Room *room, char *out, size_t out_size) {
    int cells = room->board_size * room->board_size;
    if (out_size <= (size_t)cells) {
        return;
    }

    for (int i = 0; i < cells; ++i) {
        out[i] = (room->board[i] == ' ') ? '_' : room->board[i];
    }
    out[cells] = '\0';
}

static void generate_token(ServerState *server, Room *room, int slot) {
    snprintf(room->players[slot].token,
             sizeof(room->players[slot].token),
             "%s-%c-%u-%ld",
             room->code,
             room->players[slot].mark,
             server->token_seed++,
             (long)time(NULL));
}

static int room_player_count(const Room *room) {
    return room->players[0].active + room->players[1].active;
}

static int room_is_empty(const Room *room) {
    return room_player_count(room) == 0;
}

static void clear_player(Room *room, int slot) {
    if (slot < 0 || slot > 1) {
        return;
    }

    room->players[slot].active = 0;
    room->players[slot].token[0] = '\0';
    room->players[slot].last_seen = 0;
}

static void release_room(Room *room) {
    memset(room, 0, sizeof(*room));
}

static void room_waiting_status(Room *room, char missing_mark) {
    reset_board(room);
    if (room->players[0].active || room->players[1].active) {
        set_status(room, "Player %c left room %s. Waiting for another player.", missing_mark, room->code);
    } else {
        set_status(room, "Room %s is empty.", room->code);
    }
}

static void maybe_start_game(Room *room) {
    if (room->players[0].active && room->players[1].active) {
        reset_board(room);
        room->game_started = 1;
        set_status(room, "Room %s started. Player X moves first.", room->code);
    } else {
        set_status(room, "Room %s is waiting for another player.", room->code);
    }
}

static Room *find_room(ServerState *server, const char *code) {
    if (code == NULL || code[0] == '\0') {
        return NULL;
    }

    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (server->rooms[i].in_use && strcmp(server->rooms[i].code, code) == 0) {
            return &server->rooms[i];
        }
    }

    return NULL;
}

static Room *create_room(ServerState *server, const char *code, int board_size) {
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (!server->rooms[i].in_use) {
            init_room(&server->rooms[i], code, board_size);
            return &server->rooms[i];
        }
    }
    return NULL;
}

static int parse_board_size(const char *input) {
    long value;

    if (input == NULL || input[0] == '\0') {
        return 3;
    }

    value = strtol(input, NULL, 10);
    if (value < 3 || value > MAX_BOARD_SIZE) {
        return 0;
    }
    return (int)value;
}

static int normalize_room_code(const char *input, char *out, size_t out_size) {
    size_t used = 0;

    if (out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    if (input == NULL) {
        return 0;
    }

    for (; *input != '\0'; ++input) {
        unsigned char ch = (unsigned char)*input;
        if (isalnum(ch)) {
            if (used + 1 >= out_size) {
                return 0;
            }
            out[used++] = (char)toupper(ch);
        } else if (!isspace(ch) && ch != '-' && ch != '_') {
            return 0;
        }
    }

    out[used] = '\0';
    return used >= 3;
}

static int find_player_in_room(Room *room, const char *token) {
    if (room == NULL || token == NULL || token[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < 2; ++i) {
        if (room->players[i].active && strcmp(room->players[i].token, token) == 0) {
            return i;
        }
    }

    return -1;
}

static void cleanup_inactive_rooms(ServerState *server) {
    time_t now = time(NULL);

    for (int i = 0; i < MAX_ROOMS; ++i) {
        Room *room = &server->rooms[i];
        if (!room->in_use) {
            continue;
        }

        for (int slot = 0; slot < 2; ++slot) {
            if (!room->players[slot].active) {
                continue;
            }
            if ((now - room->players[slot].last_seen) > PLAYER_TIMEOUT) {
                char mark = room->players[slot].mark;
                clear_player(room, slot);
                room_waiting_status(room, mark);
            }
        }

        if (room_is_empty(room)) {
            release_room(room);
        }
    }
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
        size_t name_len;

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

static int respond(int client_fd, const char *status, const char *content_type, const char *body) {
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

static void write_room_state_json(Room *room, int player_index, char *out, size_t out_size) {
    char board[MAX_BOARD_CELLS + 1];
    const char *role = "spectator";
    char your_mark = '-';
    int can_move = 0;

    board_to_wire(room, board, sizeof(board));

    if (player_index >= 0) {
        role = "player";
        your_mark = room->players[player_index].mark;
        if (room->game_started &&
            !room->winner &&
            !room->draw &&
            room->current_turn == player_index) {
            can_move = 1;
        }
    }

    snprintf(out,
             out_size,
             "{"
             "\"ok\":true,"
             "\"room\":\"%s\","
             "\"boardSize\":%d,"
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
             room->code,
             room->board_size,
             board,
             room->game_started ? "true" : "false",
             room->winner ? room->winner : '-',
             room->draw ? "true" : "false",
             room->players[room->current_turn].mark,
             room->status,
             role,
             your_mark,
             can_move ? "true" : "false",
             room_player_count(room));
}

static void handle_join(ServerState *server, int client_fd, const HttpRequest *request) {
    char requested_room[ROOM_CODE_SIZE];
    char normalized_room[ROOM_CODE_SIZE];
    char token[TOKEN_SIZE];
    char size_text[16];
    char body[512];
    Room *room;
    int slot;
    int requested_size;

    query_param(request->body, "room", requested_room, sizeof(requested_room));
    query_param(request->body, "token", token, sizeof(token));
    query_param(request->body, "size", size_text, sizeof(size_text));

    if (!normalize_room_code(requested_room, normalized_room, sizeof(normalized_room))) {
        respond_json(client_fd,
                     "400 Bad Request",
                     "{\"ok\":false,\"error\":\"bad_room\",\"message\":\"Use a room code with 3 to 12 letters or numbers.\"}");
        return;
    }

    requested_size = parse_board_size(size_text);
    if (requested_size == 0) {
        respond_json(client_fd,
                     "400 Bad Request",
                     "{\"ok\":false,\"error\":\"bad_size\",\"message\":\"Choose a board size from 3 to 8.\"}");
        return;
    }

    room = find_room(server, normalized_room);
    if (room == NULL) {
        room = create_room(server, normalized_room, requested_size);
    } else if (size_text[0] != '\0' && room->board_size != requested_size) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"size_locked\",\"message\":\"This room already exists with a different board size.\"}");
        return;
    }
    if (room == NULL) {
        respond_json(client_fd,
                     "503 Service Unavailable",
                     "{\"ok\":false,\"error\":\"room_limit\",\"message\":\"The server cannot create more rooms right now.\"}");
        return;
    }

    slot = find_player_in_room(room, token);
    if (slot < 0) {
        slot = room->players[0].active ? (room->players[1].active ? -1 : 1) : 0;
        if (slot < 0) {
            respond_json(client_fd,
                         "409 Conflict",
                         "{\"ok\":false,\"error\":\"room_full\",\"message\":\"This room already has two players.\"}");
            return;
        }

        room->players[slot].active = 1;
        room->players[slot].mark = (slot == 0) ? 'X' : 'O';
        room->players[slot].last_seen = time(NULL);
        generate_token(server, room, slot);
        maybe_start_game(room);
    } else {
        room->players[slot].last_seen = time(NULL);
    }

    snprintf(body,
             sizeof(body),
             "{"
             "\"ok\":true,"
             "\"room\":\"%s\","
             "\"boardSize\":%d,"
             "\"token\":\"%s\","
             "\"mark\":\"%c\","
             "\"message\":\"Joined room %s as Player %c.\""
             "}",
             room->code,
             room->board_size,
             room->players[slot].token,
             room->players[slot].mark,
             room->code,
             room->players[slot].mark);
    respond_json(client_fd, "200 OK", body);
}

static void handle_state(ServerState *server, int client_fd, const HttpRequest *request) {
    char room_code[ROOM_CODE_SIZE];
    char normalized_room[ROOM_CODE_SIZE];
    char token[TOKEN_SIZE];
    char body[512];
    Room *room;
    int slot = -1;

    query_param(request->query, "room", room_code, sizeof(room_code));
    query_param(request->query, "player", token, sizeof(token));

    if (!normalize_room_code(room_code, normalized_room, sizeof(normalized_room))) {
        respond_json(client_fd,
                     "404 Not Found",
                     "{\"ok\":false,\"error\":\"missing_room\",\"message\":\"Choose a room code first.\"}");
        return;
    }

    room = find_room(server, normalized_room);
    if (room == NULL) {
        respond_json(client_fd,
                     "404 Not Found",
                     "{\"ok\":false,\"error\":\"room_not_found\",\"message\":\"That room does not exist yet.\"}");
        return;
    }

    slot = find_player_in_room(room, token);
    if (slot >= 0) {
        room->players[slot].last_seen = time(NULL);
    }

    write_room_state_json(room, slot, body, sizeof(body));
    respond_json(client_fd, "200 OK", body);
}

static void handle_move(ServerState *server, int client_fd, const HttpRequest *request) {
    char room_code[ROOM_CODE_SIZE];
    char normalized_room[ROOM_CODE_SIZE];
    char token[TOKEN_SIZE];
    char cell_text[16];
    Room *room;
    int slot;
    long cell;
    int max_cell;

    (void)server;
    query_param(request->body, "room", room_code, sizeof(room_code));
    query_param(request->body, "player", token, sizeof(token));
    query_param(request->body, "cell", cell_text, sizeof(cell_text));

    if (!normalize_room_code(room_code, normalized_room, sizeof(normalized_room))) {
        respond_json(client_fd,
                     "400 Bad Request",
                     "{\"ok\":false,\"error\":\"bad_room\",\"message\":\"Choose a valid room code first.\"}");
        return;
    }

    room = find_room(server, normalized_room);
    if (room == NULL) {
        respond_json(client_fd,
                     "404 Not Found",
                     "{\"ok\":false,\"error\":\"room_not_found\",\"message\":\"That room does not exist.\"}");
        return;
    }

    slot = find_player_in_room(room, token);
    if (slot < 0) {
        respond_json(client_fd,
                     "403 Forbidden",
                     "{\"ok\":false,\"error\":\"invalid_player\",\"message\":\"Join the room before moving.\"}");
        return;
    }

    room->players[slot].last_seen = time(NULL);

    if (!room->game_started) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"game_not_started\",\"message\":\"Waiting for another player in this room.\"}");
        return;
    }

    if (slot != room->current_turn) {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"not_your_turn\",\"message\":\"Wait for your turn.\"}");
        return;
    }

    max_cell = room->board_size * room->board_size;
    cell = strtol(cell_text, NULL, 10);
    if (cell < 1 || cell > max_cell) {
        respond_json(client_fd,
                     "400 Bad Request",
                     "{\"ok\":false,\"error\":\"bad_cell\",\"message\":\"Cell is outside the current board.\"}");
        return;
    }

    if (room->board[cell - 1] != ' ') {
        respond_json(client_fd,
                     "409 Conflict",
                     "{\"ok\":false,\"error\":\"occupied\",\"message\":\"That cell is already taken.\"}");
        return;
    }

    room->board[cell - 1] = room->players[slot].mark;
    room->move_count++;
    room->winner = check_winner(room->board, room->board_size);

    if (room->winner) {
        room->game_started = 0;
        set_status(room, "Player %c won room %s.", room->winner, room->code);
    } else if (room->move_count == max_cell) {
        room->draw = 1;
        room->game_started = 0;
        set_status(room, "Room %s ended in a draw.", room->code);
    } else {
        room->current_turn = 1 - room->current_turn;
        set_status(room, "Room %s: Player %c to move.", room->code, room->players[room->current_turn].mark);
    }

    respond_json(client_fd,
                 "200 OK",
                 "{\"ok\":true,\"message\":\"Move accepted.\"}");
}

static void handle_leave(ServerState *server, int client_fd, const HttpRequest *request) {
    char room_code[ROOM_CODE_SIZE];
    char normalized_room[ROOM_CODE_SIZE];
    char token[TOKEN_SIZE];
    Room *room;
    int slot;

    query_param(request->body, "room", room_code, sizeof(room_code));
    query_param(request->body, "player", token, sizeof(token));

    if (!normalize_room_code(room_code, normalized_room, sizeof(normalized_room))) {
        respond_json(client_fd,
                     "200 OK",
                     "{\"ok\":true,\"message\":\"Nothing to leave.\"}");
        return;
    }

    room = find_room(server, normalized_room);
    if (room == NULL) {
        respond_json(client_fd,
                     "200 OK",
                     "{\"ok\":true,\"message\":\"Room already closed.\"}");
        return;
    }

    slot = find_player_in_room(room, token);
    if (slot >= 0) {
        char mark = room->players[slot].mark;
        clear_player(room, slot);
        room_waiting_status(room, mark);
        if (room_is_empty(room)) {
            release_room(room);
        }
    }

    respond_json(client_fd,
                 "200 OK",
                 "{\"ok\":true,\"message\":\"You left the room.\"}");
}

static void handle_request(ServerState *server, int client_fd, const HttpRequest *request) {
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
        handle_join(server, client_fd, request);
        return;
    }

    if (strcmp(request->method, "GET") == 0 && strcmp(request->path, "/api/state") == 0) {
        handle_state(server, client_fd, request);
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->path, "/api/move") == 0) {
        handle_move(server, client_fd, request);
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->path, "/api/leave") == 0) {
        handle_leave(server, client_fd, request);
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

    ServerState server;
    init_server(&server);

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

        cleanup_inactive_rooms(&server);

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
            handle_request(&server, client_fd, &request);
        } else {
            respond_text(client_fd, "400 Bad Request", "Bad request");
        }

        close(client_fd);
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
