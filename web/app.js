const boardEl = document.getElementById("board");
const joinBtn = document.getElementById("joinBtn");
const leaveBtn = document.getElementById("leaveBtn");
const playerMarkEl = document.getElementById("playerMark");
const statusTextEl = document.getElementById("statusText");
const playersTextEl = document.getElementById("playersText");
const turnBadgeEl = document.getElementById("turnBadge");

const state = {
  token: sessionStorage.getItem("ttt-player-token") || "",
  board: "_________",
  canMove: false,
  yourMark: "-",
  polling: null
};

function createBoard() {
  boardEl.innerHTML = "";
  for (let i = 0; i < 9; i += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "cell";
    button.dataset.cell = String(i + 1);
    button.textContent = String(i + 1);
    button.addEventListener("click", () => submitMove(i + 1));
    boardEl.appendChild(button);
  }
}

function saveToken(token) {
  state.token = token || "";
  if (state.token) {
    sessionStorage.setItem("ttt-player-token", state.token);
  } else {
    sessionStorage.removeItem("ttt-player-token");
  }
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: {
      "Content-Type": "application/x-www-form-urlencoded"
    },
    ...options
  });

  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.message || "Request failed");
  }
  return data;
}

async function joinGame() {
  try {
    const body = new URLSearchParams();
    if (state.token) {
      body.set("token", state.token);
    }
    const data = await api("/api/join", {
      method: "POST",
      body
    });
    saveToken(data.token);
    statusTextEl.textContent = data.message;
    joinBtn.disabled = true;
    leaveBtn.disabled = false;
    await refreshState();
  } catch (error) {
    statusTextEl.textContent = error.message;
  }
}

async function leaveGame() {
  if (!state.token) {
    return;
  }

  try {
    const body = new URLSearchParams({ player: state.token });
    await api("/api/leave", {
      method: "POST",
      body
    });
  } catch (error) {
    statusTextEl.textContent = error.message;
  } finally {
    saveToken("");
    state.canMove = false;
    state.yourMark = "-";
    joinBtn.disabled = false;
    leaveBtn.disabled = true;
    playerMarkEl.textContent = "Not joined";
  }
}

function updateBoard(board, canMove) {
  state.board = board;
  const cells = boardEl.querySelectorAll(".cell");
  cells.forEach((cellEl, index) => {
    const value = board[index];
    const empty = value === "_";
    cellEl.textContent = empty ? String(index + 1) : value;
    cellEl.disabled = !empty || !canMove;
    cellEl.classList.toggle("filled", !empty);
  });
}

function updateUi(snapshot) {
  state.canMove = Boolean(snapshot.canMove);
  state.yourMark = snapshot.yourMark;

  updateBoard(snapshot.board, state.canMove);

  playerMarkEl.textContent = snapshot.role === "player" ? `Player ${snapshot.yourMark}` : "Spectator";
  statusTextEl.textContent = snapshot.status;
  playersTextEl.textContent = `${snapshot.playersConnected} / 2`;

  if (snapshot.winner !== "-") {
    turnBadgeEl.textContent = `Winner: ${snapshot.winner}`;
  } else if (snapshot.draw) {
    turnBadgeEl.textContent = "Draw";
  } else if (!snapshot.gameStarted) {
    turnBadgeEl.textContent = "Waiting";
  } else {
    turnBadgeEl.textContent = `Turn: ${snapshot.currentTurn}`;
  }

  joinBtn.disabled = snapshot.role === "player";
  leaveBtn.disabled = snapshot.role !== "player";
}

async function refreshState() {
  try {
    const suffix = state.token ? `?player=${encodeURIComponent(state.token)}` : "";
    const snapshot = await api(`/api/state${suffix}`, { method: "GET" });
    updateUi(snapshot);
  } catch (error) {
    statusTextEl.textContent = error.message;
  }
}

async function submitMove(cell) {
  if (!state.token || !state.canMove) {
    return;
  }

  try {
    const body = new URLSearchParams({
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
  if (!state.token) {
    return;
  }

  const body = new URLSearchParams({ player: state.token });
  navigator.sendBeacon("/api/leave", body);
});

joinBtn.addEventListener("click", joinGame);
leaveBtn.addEventListener("click", leaveGame);

createBoard();
refreshState();
state.polling = window.setInterval(refreshState, 1000);
