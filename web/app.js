const boardEl = document.getElementById("board");
const joinBtn = document.getElementById("joinBtn");
const leaveBtn = document.getElementById("leaveBtn");
const roomInput = document.getElementById("roomInput");
const roomLabelEl = document.getElementById("roomLabel");
const playerMarkEl = document.getElementById("playerMark");
const statusTextEl = document.getElementById("statusText");
const playersTextEl = document.getElementById("playersText");
const turnTextEl = document.getElementById("turnText");
const turnBadgeEl = document.getElementById("turnBadge");
const themeBtn = document.getElementById("themeBtn");

const state = {
  token: sessionStorage.getItem("ttt-player-token") || "",
  room: sessionStorage.getItem("ttt-room-code") || "",
  canMove: false,
  polling: null,
  theme: localStorage.getItem("ttt-theme") || "light"
};

function normalizeRoom(value) {
  return value.toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 12);
}

function createBoard() {
  boardEl.innerHTML = "";
  for (let i = 0; i < 9; i += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "cell";
    button.textContent = String(i + 1);
    button.addEventListener("click", () => submitMove(i + 1));
    boardEl.appendChild(button);
  }
}

function saveSession(token, room) {
  state.token = token || "";
  state.room = room || "";

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
}

function applyTheme(theme) {
  state.theme = theme === "dark" ? "dark" : "light";
  document.documentElement.dataset.theme = state.theme;
  localStorage.setItem("ttt-theme", state.theme);
  themeBtn.textContent = state.theme === "dark" ? "Light mode" : "Dark mode";
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
  roomInput.disabled = joined;
}

function renderBoard(board, canMove) {
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
  renderBoard("_________", false);
  playerMarkEl.textContent = "Not joined";
  playersTextEl.textContent = "0 / 2";
  turnTextEl.textContent = "Waiting";
  turnBadgeEl.textContent = "Idle";
  roomLabelEl.textContent = state.room || "None";
}

function updateUi(snapshot) {
  state.canMove = Boolean(snapshot.canMove);

  renderBoard(snapshot.board, state.canMove);
  roomLabelEl.textContent = snapshot.room;
  playerMarkEl.textContent = snapshot.role === "player" ? `Player ${snapshot.yourMark}` : "Spectator";
  playersTextEl.textContent = `${snapshot.playersConnected} / 2`;
  statusTextEl.textContent = snapshot.status;

  if (snapshot.winner !== "-") {
    turnTextEl.textContent = `Winner ${snapshot.winner}`;
    turnBadgeEl.textContent = `Winner ${snapshot.winner}`;
  } else if (snapshot.draw) {
    turnTextEl.textContent = "Draw";
    turnBadgeEl.textContent = "Draw";
  } else if (!snapshot.gameStarted) {
    turnTextEl.textContent = "Waiting";
    turnBadgeEl.textContent = "Waiting";
  } else {
    turnTextEl.textContent = `Player ${snapshot.currentTurn}`;
    turnBadgeEl.textContent = `Turn ${snapshot.currentTurn}`;
  }

  setJoinedState(snapshot.role === "player");
}

async function joinRoom() {
  const desiredRoom = normalizeRoom(roomInput.value || state.room);
  roomInput.value = desiredRoom;

  if (desiredRoom.length < 3) {
    statusTextEl.textContent = "Use a room code with at least 3 letters or numbers.";
    return;
  }

  try {
    const body = new URLSearchParams({ room: desiredRoom });
    if (state.token) {
      body.set("token", state.token);
    }
    const data = await api("/api/join", {
      method: "POST",
      body
    });

    saveSession(data.token, data.room);
    roomInput.value = data.room;
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
    saveSession("", state.room);
    setJoinedState(false);
    playerMarkEl.textContent = "Not joined";
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
      roomInput.value = "";
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

roomInput.addEventListener("input", () => {
  roomInput.value = normalizeRoom(roomInput.value);
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

createBoard();
applyTheme(state.theme);
roomInput.value = state.room;
resetView();
refreshState();
state.polling = window.setInterval(refreshState, 1000);
