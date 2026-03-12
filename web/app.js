const boardEl = document.getElementById("board");
const joinBtn = document.getElementById("joinBtn");
const leaveBtn = document.getElementById("leaveBtn");
const generateBtn = document.getElementById("generateBtn");
const usernameInput = document.getElementById("usernameInput");
const roomInput = document.getElementById("roomInput");
const sizeSelect = document.getElementById("sizeSelect");
const roomLabelEl = document.getElementById("roomLabel");
const playerMarkEl = document.getElementById("playerMark");
const playerNameEl = document.getElementById("playerName");
const statusTextEl = document.getElementById("statusText");
const playersTextEl = document.getElementById("playersText");
const sizeTextEl = document.getElementById("sizeText");
const turnBadgeEl = document.getElementById("turnBadge");
const themeBtn = document.getElementById("themeBtn");
const themeIcon = document.getElementById("themeIcon");

const state = {
  token: sessionStorage.getItem("ttt-player-token") || "",
  room: sessionStorage.getItem("ttt-room-code") || "",
  username: sessionStorage.getItem("ttt-username") || "",
  boardSize: Number(sessionStorage.getItem("ttt-board-size") || "3"),
  canMove: false,
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

function saveSession(token, room, boardSize = state.boardSize, username = state.username) {
  state.token = token || "";
  state.room = room || "";
  state.username = username || "";
  state.boardSize = boardSize;

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
    cellEl.textContent = empty ? String(index + 1) : value;
    cellEl.disabled = !empty || !canMove;
    cellEl.classList.toggle("filled", !empty);
  });
}

function resetView() {
  createBoard(state.boardSize);
  renderBoard("_".repeat(state.boardSize * state.boardSize), false);
  playerMarkEl.textContent = "Not joined";
  playerNameEl.textContent = state.username || "Guest";
  playersTextEl.textContent = "0 / 2";
  sizeTextEl.textContent = `${state.boardSize} x ${state.boardSize}`;
  turnBadgeEl.textContent = "Idle";
  roomLabelEl.textContent = state.room || "None";
}

function updateUi(snapshot) {
  state.canMove = Boolean(snapshot.canMove);
  state.boardSize = Number(snapshot.boardSize);
  sizeSelect.value = String(state.boardSize);

  renderBoard(snapshot.board, state.canMove);
  roomLabelEl.textContent = snapshot.room;
  playerMarkEl.textContent = snapshot.role === "player" ? `Player ${snapshot.yourMark}` : "Spectator";
  playerNameEl.textContent = snapshot.role === "player" ? snapshot.yourName : (state.username || "Guest");
  playersTextEl.textContent = `${snapshot.playersConnected} / 2`;
  statusTextEl.textContent = snapshot.status;
  sizeTextEl.textContent = `${snapshot.boardSize} x ${snapshot.boardSize}`;

  if (snapshot.winner !== "-") {
    turnBadgeEl.textContent = `Winner ${snapshot.winner}`;
  } else if (snapshot.draw) {
    turnBadgeEl.textContent = "Draw";
  } else if (!snapshot.gameStarted) {
    turnBadgeEl.textContent = "Waiting";
  } else {
    turnBadgeEl.textContent = `Turn ${snapshot.currentTurn}`;
  }

  setJoinedState(snapshot.role === "player");
}

async function joinRoom() {
  const desiredRoom = normalizeRoom(roomInput.value || state.room);
  const desiredUsername = normalizeUsername(usernameInput.value || state.username);
  roomInput.value = desiredRoom;
  usernameInput.value = desiredUsername;

  if (desiredRoom.length < 3) {
    statusTextEl.textContent = "Use a 5-digit room code.";
    return;
  }

  if (desiredUsername.length < 2) {
    statusTextEl.textContent = "Use a username with at least 2 characters.";
    return;
  }

  try {
    const requestedSize = String(Number(sizeSelect.value || state.boardSize || 3));
    const body = new URLSearchParams({ room: desiredRoom, size: requestedSize, username: desiredUsername });
    if (state.token) {
      body.set("token", state.token);
    }
    const data = await api("/api/join", {
      method: "POST",
      body
    });

    saveSession(data.token, data.room, Number(data.boardSize || requestedSize), data.username || desiredUsername);
    roomInput.value = data.room;
    usernameInput.value = state.username;
    sizeSelect.value = String(state.boardSize);
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
    saveSession("", state.room, state.boardSize, state.username);
    setJoinedState(false);
    playerMarkEl.textContent = "Not joined";
    playerNameEl.textContent = state.username || "Guest";
    sizeSelect.value = String(state.boardSize);
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
resetView();
refreshState();
state.polling = window.setInterval(refreshState, 1000);
