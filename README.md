# Tic-Tac-Toe Web Final Project

## Architecture Summary

This project is now a browser-based Tic-Tac-Toe game backed by a POSIX C server, with support for multiple custom rooms, host-selected board sizes, and configurable connect lengths.

- `server.c` is an HTTP server that owns the full game state for many rooms, validates all moves, assigns Player X and Player O inside each room, and handles disconnects by timing out inactive browser sessions.
- `web/index.html`, `web/app.js`, and `web/styles.css` provide the browser UI, room-code entry flow, host-selected board sizes, and icon-based light/dark theme switcher.
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
- `POST /api/join`: joins or creates a room using a custom room code, `size=<3-8>`, `win=<3-size>`, and a username
- `GET /api/state?room=<code>&player=<token>`: returns the board, board size, connect length, turn, winner, draw flag, and status text for one room
- `POST /api/move`: submits `room=<code>&player=<token>&cell=<1-(size*size)>`
- `POST /api/leave`: removes the player from one room

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

- Enter the same 5-digit room code in two tabs or two browsers.
- Enter a custom username before joining.
- The host can choose the board size from `3x3` up to `8x8`.
- The default winning length is `3`, and the host can increase it up to the current board size.
- Click `Join Room` in each browser session.
- The first player in that room becomes `X`, the second becomes `O`.
- Click an empty square when it is your turn.
- Use the theme button to switch between light and dark mode.
- Click `Leave Room` to quit the room.

## Deploying Publicly

This server is a plain HTTP service written in C. To let people access it globally, deploy it on a Linux server or VPS with a public IP address.

### 1. Prepare a Linux Server

Typical options:

- Ubuntu VPS on DigitalOcean, Hetzner, Linode, AWS, or similar
- A home Linux machine with port forwarding configured on the router

Install build tools on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential
```

### 2. Upload the Project

Copy the project to the server:

```bash
scp -r ./Network-Programming-CS-G6 user@your-server-ip:/home/user/tictactoe
```

Or clone it directly on the server if it is in a Git repository.

### 3. Build on the Server

```bash
cd /home/user/tictactoe
make
```

### 4. Run the Server

Example on port `8784`:

```bash
./server 8784
```

People can then access it using:

```text
http://your-server-ip:8784/
```

### 5. Open the Firewall

If your server uses `ufw`:

```bash
sudo ufw allow 8784/tcp
sudo ufw status
```

If you are behind a router at home, forward external TCP port `8784` to the machine running the server.

### 6. Keep It Running

Simple background run:

```bash
nohup ./server 8784 > server.log 2>&1 &
```

Better option with `systemd`:

Create `/etc/systemd/system/tictactoe.service`:

```ini
[Unit]
Description=Tic-Tac-Toe Web Server
After=network.target

[Service]
User=youruser
WorkingDirectory=/home/youruser/tictactoe
ExecStart=/home/youruser/tictactoe/server 8784
Restart=always

[Install]
WantedBy=multi-user.target
```

Then enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now tictactoe
sudo systemctl status tictactoe
```

### 7. Optional: Put It Behind Nginx

If you want a cleaner public URL such as `http://game.yourdomain.com/` or later add HTTPS with Let's Encrypt, place Nginx in front of the C server.

Example Nginx site config:

```nginx
server {
    listen 80;
    server_name game.yourdomain.com;

    location / {
        proxy_pass http://127.0.0.1:8784;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }
}
```

Reload Nginx:

```bash
sudo nginx -t
sudo systemctl reload nginx
```

Then users can access:

```text
http://game.yourdomain.com/
```

### 8. Optional: HTTPS

For public access, HTTPS is recommended. If you have a domain and Nginx installed, Certbot is the usual next step:

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d game.yourdomain.com
```

After that, players can use:

```text
https://game.yourdomain.com/
```
