const boardEl = document.getElementById("board");
const joinBtn = document.getElementById("joinBtn");
const leaveBtn = document.getElementById("leaveBtn");
const generateBtn = document.getElementById("generateBtn");
const usernameInput = document.getElementById("usernameInput");
const roomInput = document.getElementById("roomInput");
const sizeSelect = document.getElementById("sizeSelect");
const winSelect = document.getElementById("winSelect");
const roomLabelEl = document.getElementById("roomLabel");
const playerMarkEl = document.getElementById("playerMark");
const playerNameEl = document.getElementById("playerName");
const statusTextEl = document.getElementById("statusText");
const playersTextEl = document.getElementById("playersText");
const sizeTextEl = document.getElementById("sizeText");
const turnBadgeEl = document.getElementById("turnBadge");
const themeBtn = document.getElementById("themeBtn");
const themeIcon = document.getElementById("themeIcon");
const announceBox = document.getElementById("announceBox");
const confettiLayer = document.getElementById("confettiLayer");

const state = {
  token: sessionStorage.getItem("ttt-player-token") || "",
  room: sessionStorage.getItem("ttt-room-code") || "",
  username: sessionStorage.getItem("ttt-username") || "",
  boardSize: Number(sessionStorage.getItem("ttt-board-size") || "3"),
  winLength: Number(sessionStorage.getItem("ttt-win-length") || "3"),
  canMove: false,
  lastBoard: "",
  lastWinner: "-",
  lastDraw: false,
  polling: null,
  theme: localStorage.getItem("ttt-theme") || "light"
};

function normalizeRoom(value) {
  return value.replace(/\D/g, "").slice(0, 5);
}

function generateRoomCode() {
  let code = "";
  for (let i = 0; i < 5; i += 1) {
    code += Math.floor(Math.random() * 10);
  }
  return code;
}

function normalizeUsername(value) {
  return value.replace(/[^\w\s-]/g, "").trim().slice(0, 20);
}

function createBoard(size) {
  boardEl.innerHTML = "";
  boardEl.style.setProperty("--board-size", String(size));
  boardEl.dataset.size = String(size);

  for (let i = 0; i < size * size; i += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "cell";
    button.textContent = String(i + 1);
    button.addEventListener("click", () => submitMove(i + 1));
    boardEl.appendChild(button);
  }
}

function syncWinOptions() {
  const boardSize = Number(sizeSelect.value || state.boardSize || 3);
  const currentWin = Number(winSelect.value || state.winLength || 3);

  Array.from(winSelect.options).forEach((option) => {
    option.disabled = Number(option.value) > boardSize;
  });

  if (currentWin > boardSize) {
    winSelect.value = String(Math.max(3, boardSize));
  }
}

function saveSession(token, room, boardSize = state.boardSize, username = state.username, winLength = state.winLength) {
  state.token = token || "";
  state.room = room || "";
  state.username = username || "";
  state.boardSize = boardSize;
  state.winLength = winLength;

  if (state.token) {
    sessionStorage.setItem("ttt-player-token", state.token);
  } else {
    sessionStorage.removeItem("ttt-player-token");
  }

  if (state.room) {
    sessionStorage.setItem("ttt-room-code", state.room);
  } else {
    sessionStorage.removeItem("ttt-room-code");
  }

  if (state.username) {
    sessionStorage.setItem("ttt-username", state.username);
  } else {
    sessionStorage.removeItem("ttt-username");
  }

  sessionStorage.setItem("ttt-board-size", String(state.boardSize));
  sessionStorage.setItem("ttt-win-length", String(state.winLength));
}

function applyTheme(theme) {
  state.theme = theme === "dark" ? "dark" : "light";
  document.documentElement.dataset.theme = state.theme;
  localStorage.setItem("ttt-theme", state.theme);
  themeBtn.setAttribute("aria-label", state.theme === "dark" ? "Switch to light mode" : "Switch to dark mode");
  themeIcon.textContent = state.theme === "dark" ? "☀" : "☾";
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: {
      "Content-Type": "application/x-www-form-urlencoded"
    },
    ...options
  });

  const data = await response.json();
  if (!response.ok || data.ok === false) {
    throw new Error(data.message || "Request failed");
  }
  return data;
}

function setJoinedState(joined) {
  joinBtn.disabled = joined;
  leaveBtn.disabled = !joined;
  usernameInput.disabled = joined;
  roomInput.disabled = joined;
  sizeSelect.disabled = joined;
  winSelect.disabled = joined;
  generateBtn.disabled = joined;
}

function renderBoard(board, canMove) {
  if (boardEl.childElementCount !== board.length) {
    createBoard(state.boardSize);
  }
  const cells = boardEl.querySelectorAll(".cell");
  cells.forEach((cellEl, index) => {
    const value = board[index];
    const empty = value === "_";
    const previous = state.lastBoard[index];
    cellEl.textContent = empty ? String(index + 1) : value;
    cellEl.disabled = !empty || !canMove;
    cellEl.classList.toggle("filled", !empty);
    cellEl.classList.remove("placed-x", "placed-o");
    if (previous && previous === "_" && !empty) {
      cellEl.classList.add(value === "X" ? "placed-x" : "placed-o");
    }
  });
  state.lastBoard = board;
}

function showAnnouncement(message, tone) {
  announceBox.textContent = message;
  announceBox.className = `announce-box ${tone}`;
  announceBox.hidden = false;
}

function hideAnnouncement() {
  announceBox.hidden = true;
  announceBox.className = "announce-box";
}

function launchConfetti() {
  confettiLayer.innerHTML = "";
  for (let i = 0; i < 28; i += 1) {
    const piece = document.createElement("span");
    piece.className = "confetti-piece";
    piece.style.left = `${Math.random() * 100}%`;
    piece.style.animationDelay = `${Math.random() * 0.25}s`;
    piece.style.background = i % 3 === 0 ? "#22c55e" : (i % 3 === 1 ? "#f59e0b" : "#38bdf8");
    confettiLayer.appendChild(piece);
  }
  window.setTimeout(() => {
    confettiLayer.innerHTML = "";
  }, 2400);
}

function resetView() {
  createBoard(state.boardSize);
  renderBoard("_".repeat(state.boardSize * state.boardSize), false);
  playerMarkEl.textContent = "Not joined";
  playerNameEl.textContent = state.username || "Guest";
  playersTextEl.textContent = "0 / 2";
  sizeTextEl.textContent = `${state.boardSize} x ${state.boardSize} / C${state.winLength}`;
  turnBadgeEl.textContent = "Idle";
  roomLabelEl.textContent = state.room || "None";
  winSelect.value = String(state.winLength);
  hideAnnouncement();
  state.lastWinner = "-";
  state.lastDraw = false;
}

function updateUi(snapshot) {
  state.canMove = Boolean(snapshot.canMove);
  state.boardSize = Number(snapshot.boardSize);
  state.winLength = Number(snapshot.winLength);
  sizeSelect.value = String(state.boardSize);
  winSelect.value = String(state.winLength);
  syncWinOptions();

  renderBoard(snapshot.board, state.canMove);
  roomLabelEl.textContent = snapshot.room;
  playerMarkEl.textContent = snapshot.role === "player" ? `Player ${snapshot.yourMark}` : "Spectator";
  playerNameEl.textContent = snapshot.role === "player" ? snapshot.yourName : (state.username || "Guest");
  playersTextEl.textContent = `${snapshot.playersConnected} / 2`;
  statusTextEl.textContent = snapshot.status;
  sizeTextEl.textContent = `${snapshot.boardSize} x ${snapshot.boardSize} / C${snapshot.winLength}`;

  if (snapshot.winner !== "-") {
    turnBadgeEl.textContent = `Winner ${snapshot.winner}`;
    if (snapshot.role === "player") {
      if (snapshot.winner === snapshot.yourMark && state.lastWinner !== snapshot.winner) {
        showAnnouncement(`Victory: ${snapshot.yourName} connected ${snapshot.winLength}.`, "win");
        launchConfetti();
      } else if (snapshot.winner !== snapshot.yourMark && state.lastWinner !== snapshot.winner) {
        showAnnouncement(`Defeat: ${snapshot.winner} took the round.`, "lose");
      }
    }
    state.lastDraw = false;
  } else if (snapshot.draw) {
    turnBadgeEl.textContent = "Draw";
    if (!state.lastDraw) {
      showAnnouncement("Draw game.", "draw");
    }
    state.lastDraw = true;
  } else if (!snapshot.gameStarted) {
    turnBadgeEl.textContent = "Waiting";
    hideAnnouncement();
    state.lastDraw = false;
  } else {
    turnBadgeEl.textContent = `Turn ${snapshot.currentTurn}`;
    hideAnnouncement();
    state.lastDraw = false;
  }

  state.lastWinner = snapshot.winner;
  setJoinedState(snapshot.role === "player");
}

async function joinRoom() {
  const desiredRoom = normalizeRoom(roomInput.value || state.room);
  const desiredUsername = normalizeUsername(usernameInput.value || state.username);
  roomInput.value = desiredRoom;
  usernameInput.value = desiredUsername;

  if (desiredRoom.length !== 5) {
    statusTextEl.textContent = "Use a 5-digit room code.";
    return;
  }

  if (desiredUsername.length < 2) {
    statusTextEl.textContent = "Use a username with at least 2 characters.";
    return;
  }

  try {
    const requestedSize = String(Number(sizeSelect.value || state.boardSize || 3));
    syncWinOptions();
    const requestedWin = String(Number(winSelect.value || state.winLength || 3));
    const body = new URLSearchParams({
      room: desiredRoom,
      size: requestedSize,
      win: requestedWin,
      username: desiredUsername
    });
    if (state.token) {
      body.set("token", state.token);
    }
    const data = await api("/api/join", {
      method: "POST",
      body
    });

    saveSession(
      data.token,
      data.room,
      Number(data.boardSize || requestedSize),
      data.username || desiredUsername,
      Number(data.winLength || requestedWin)
    );
    roomInput.value = data.room;
    usernameInput.value = state.username;
    sizeSelect.value = String(state.boardSize);
    winSelect.value = String(state.winLength);
    statusTextEl.textContent = data.message;
    setJoinedState(true);
    await refreshState();
  } catch (error) {
    statusTextEl.textContent = error.message;
  }
}

async function leaveRoom() {
  if (!state.token || !state.room) {
    return;
  }

  try {
    const body = new URLSearchParams({
      room: state.room,
      player: state.token
    });
    await api("/api/leave", {
      method: "POST",
      body
    });
  } catch (error) {
    statusTextEl.textContent = error.message;
  } finally {
    saveSession("", state.room, state.boardSize, state.username, state.winLength);
    setJoinedState(false);
    playerMarkEl.textContent = "Not joined";
    playerNameEl.textContent = state.username || "Guest";
    sizeSelect.value = String(state.boardSize);
    winSelect.value = String(state.winLength);
  }
}

async function refreshState() {
  if (!state.room) {
    resetView();
    return;
  }

  try {
    const params = new URLSearchParams({ room: state.room });
    if (state.token) {
      params.set("player", state.token);
    }
    const snapshot = await api(`/api/state?${params.toString()}`, { method: "GET" });
    updateUi(snapshot);
  } catch (error) {
    setJoinedState(false);
    if (error.message.includes("does not exist")) {
      saveSession("", "");
      usernameInput.value = state.username;
      roomInput.value = "";
      sizeSelect.value = String(state.boardSize);
      roomLabelEl.textContent = "None";
      winSelect.value = String(state.winLength);
    }
    statusTextEl.textContent = error.message;
  }
}

async function submitMove(cell) {
  if (!state.token || !state.room || !state.canMove) {
    return;
  }

  try {
    const body = new URLSearchParams({
      room: state.room,
      player: state.token,
      cell: String(cell)
    });
    const result = await api("/api/move", {
      method: "POST",
      body
    });
    statusTextEl.textContent = result.message;
    await refreshState();
  } catch (error) {
    statusTextEl.textContent = error.message;
    await refreshState();
  }
}

window.addEventListener("beforeunload", () => {
  if (!state.token || !state.room) {
    return;
  }

  const body = new URLSearchParams({
    room: state.room,
    player: state.token
  });
  navigator.sendBeacon("/api/leave", body);
});

usernameInput.addEventListener("input", () => {
  usernameInput.value = normalizeUsername(usernameInput.value);
});

roomInput.addEventListener("input", () => {
  roomInput.value = normalizeRoom(roomInput.value);
});

generateBtn.addEventListener("click", () => {
  roomInput.value = generateRoomCode();
});

sizeSelect.addEventListener("change", () => {
  syncWinOptions();
});

roomInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    joinRoom();
  }
});

joinBtn.addEventListener("click", joinRoom);
leaveBtn.addEventListener("click", leaveRoom);
themeBtn.addEventListener("click", () => {
  applyTheme(state.theme === "dark" ? "light" : "dark");
});

createBoard(state.boardSize);
applyTheme(state.theme);
usernameInput.value = state.username;
roomInput.value = state.room;
sizeSelect.value = String(state.boardSize);
winSelect.value = String(state.winLength);
syncWinOptions();
resetView();
refreshState();
state.polling = window.setInterval(refreshState, 1000);
