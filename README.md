# Tic-Tac-Toe Final Project

## Architecture Summary

This project is a small TCP client-server Tic-Tac-Toe game written in POSIX C for Linux/macOS.

- `server.c` owns the full game state: player slots, board, turns, move validation, win/draw logic, and disconnect handling.
- `client.c` is a terminal client that connects to the server, displays game updates, and lets the user type moves.
- `protocol.h` contains the shared application-layer protocol constants and framed `send`/`recv` helpers.
- Both server and client use `select()` so they stay responsive without threads:
  - the server multiplexes the listening socket plus two player sockets
  - the client multiplexes the socket plus terminal input

TCP is used because Tic-Tac-Toe needs reliable, ordered delivery. A move or board update must not be lost or arrive out of order.

## Protocol Definition

Each message is sent as:

1. 4-byte unsigned length in network byte order
2. ASCII payload with a single command line

This framing prevents bugs caused by partial reads/writes in TCP streams.

### Commands

- `JOIN X` or `JOIN O`: server assigns a player mark after connection
- `INFO waiting_for_second_player`: first player is connected, waiting
- `START X`: game starts, `X` moves first
- `BOARD XOX_O____`: current board, `_` means empty
- `YOUR_TURN`: the client may send a move
- `WAIT`: wait for the opponent
- `MOVE 5`: client requests to place its mark in cell 5
- `OK`: move accepted
- `ERR reason`: invalid command or invalid move
- `WIN X`: player `X` won
- `DRAW`: game ended in a draw
- `QUIT reason`: game/session ended because a player quit or disconnected

## Files

- `server.c`
- `client.c`
- `protocol.h`
- `Makefile`
- `README.md`

## Build

```bash
make
```

## Run

Start the server:

```bash
./server 8784
```

Start two clients in separate terminals:

```bash
./client 127.0.0.1 8784
```

You can also use a hostname instead of an IP address:

```bash
./client localhost 8784
```

## How to Play

- The board is shown with numbers `1` to `9` for empty cells.
- When the server sends `YOUR_TURN`, enter a number from `1` to `9`.
- Type `quit` to leave the session.

Board positions:

```text
 1 | 2 | 3
---+---+---
 4 | 5 | 6
---+---+---
 7 | 8 | 9
```

## How This Project Uses Course Knowledge

This project directly applies the course networking topics:

- Network concepts: it uses a clear client-server model where the server is authoritative and clients are peers only through the server.
- Socket programming: the server performs `socket`, `setsockopt(SO_REUSEADDR)`, `bind`, `listen`, and `accept`; the client performs hostname/IP resolution and `connect`.
- Message framing: TCP is a byte stream, so each application message is length-prefixed to handle partial `recv` and partial `send`.
- Disconnect handling: the server detects `recv == 0` or quit messages, informs the remaining player, and closes sockets cleanly.
- Asynchronous/nonblocking ideas: instead of blocking on one source at a time, both programs use `select()` for responsive multiplexed I/O.
- Multiplexing: the server watches multiple sockets at once; the client watches both keyboard input and the network socket.
- Reuse of lecture samples: the basic socket lifecycle and `SO_REUSEADDR` usage follow the sample `server.cpp` and `client.cpp`, extended into a framed multi-message protocol and a full game state machine.
