# Tic-Tac-Toe Web Final Project

## Architecture Summary

This project is now a browser-based Tic-Tac-Toe game backed by a POSIX C server.

- `server.c` is an HTTP server that owns the full game state, validates all moves, assigns Player X and Player O, and handles disconnects by timing out inactive browser sessions.
- `web/index.html`, `web/app.js`, and `web/styles.css` provide the browser UI.
- The browser polls the server once per second for board updates, while moves are submitted with small HTTP `POST` requests.
- The server uses `select()` around the listening socket so the design still demonstrates multiplexing and event-driven waiting without threads.

TCP is still the transport because HTTP runs on top of TCP. Reliable, ordered delivery is required so join requests, moves, and board updates are processed consistently.

## HTTP Protocol

Instead of a terminal client and custom text frames, this version uses HTTP as the application-layer protocol. HTTP already provides message framing through:

1. A request/status line
2. Headers such as `Content-Length`
3. A body of the declared length

### Endpoints

- `GET /`: serves the game page
- `GET /app.js`: serves browser logic
- `GET /styles.css`: serves page styling
- `POST /api/join`: assigns a free player slot and returns a player token
- `GET /api/state?player=<token>`: returns the board, turn, winner, draw flag, and status text
- `POST /api/move`: submits `player=<token>&cell=<1-9>`
- `POST /api/leave`: removes the player from the current game

The server is authoritative. The browser never decides whether a move is legal; it only sends requests and renders the JSON state returned by the server.

## Files

- `server.c`
- `web/index.html`
- `web/app.js`
- `web/styles.css`
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

Then open this URL in two browser tabs or on two machines:

```text
http://localhost:8784/
```

If you are testing from another device on the same network, replace `localhost` with the server machine's IP address.

## How to Play

- Open the page in two tabs.
- Click `Join Game` in each tab.
- The first joined player becomes `X`, the second becomes `O`.
- Click an empty square when it is your turn.
- Click `Leave Game` to quit the round.

## How This Project Uses Course Knowledge

This version still uses the course networking ideas directly:

- Network concepts: the game remains client-server, with the server acting as the single source of truth.
- Socket programming: the server uses `socket`, `setsockopt(SO_REUSEADDR)`, `bind`, `listen`, `accept`, `recv`, `send`, and `close`.
- Message framing: HTTP request/response framing uses headers and `Content-Length`, which solves the partial-read problem on top of TCP streams.
- Disconnect handling: browser sessions are tracked with player tokens and last-seen timestamps; inactive players are timed out and removed cleanly.
- Asynchronous or nonblocking ideas: the browser remains responsive independently of the network, and the server uses `select()` with a timeout to combine request handling with cleanup work.
- Multiplexing: the server waits on the listening socket and periodically handles inactive-session cleanup in the same event loop.
- Reuse of lecture ideas: the original socket lifecycle from the sample TCP server is preserved, but the user-facing client has been upgraded from a terminal program to a web frontend.
