// ============================================================================
// app.js — Game controller for the 4×4 WASM Tic-Tac-Toe
//
// Architecture:
//   1. Load WASM engine module (createEngineModule from engine.js)
//   2. Load opening_book.json
//   3. Player selects side (X or O)
//   4. On computer's turn:
//        - Move 1 and (if applicable) Move 2: serve from opening book cache
//          using D₈ symmetry mapping so the response fits the actual board
//        - All later moves: call wasm_getBestMove() — sub-ms with warm TT
//   5. Win / draw handled by wasm_checkWin() / wasm_isDraw()
// ============================================================================

const BOARD_SIZE = 4;
const TARGET = 4;

// ---- State ------------------------------------------------------------------
let engine = null;   // WASM module after load
let openingBook = null;  // parsed opening_book.json
let engineReady = false;

let humanPlayer = 0;    // 1=X, 2=O
let cpuPlayer = 0;
let cells = new Int32Array(BOARD_SIZE * BOARD_SIZE); // 0/1/2
let moveNumber = 0;    // total plies played
let gameOver = false;
let cpuCellsPtr = null; // WASM heap allocation (reused)

// D₈ transforms for a (BOARD_SIZE - 1) × (BOARD_SIZE - 1) grid ---------
const SZ = BOARD_SIZE - 1; // convenience: used in transform math
const D8_TRANSFORMS = [
  ([r, c]) => [r, c],  // 0: identity
  ([r, c]) => [c, SZ - r],  // 1: 90° CW
  ([r, c]) => [SZ - r, SZ - c],  // 2: 180°
  ([r, c]) => [SZ - c, r],  // 3: 270° CW
  ([r, c]) => [r, SZ - c],  // 4: flip horizontal
  ([r, c]) => [SZ - r, c],  // 5: flip vertical
  ([r, c]) => [c, r],  // 6: flip main diagonal
  ([r, c]) => [SZ - c, SZ - r],  // 7: flip anti-diagonal
];

// ---- Utility ----------------------------------------------------------------

function cellIdx(r, c) { return r * BOARD_SIZE + c; }
function cellRC(idx) { return [Math.floor(idx / BOARD_SIZE), idx % BOARD_SIZE]; }

// Find the D₈ transform T such that T([fr, fc]) == [tr, tc].
// Returns the transform function, or null if none matches.
function findTransform([fr, fc], [tr, tc]) {
  for (const t of D8_TRANSFORMS) {
    const [nr, nc] = t([fr, fc]);
    if (nr === tr && nc === tc) return t;
  }
  return null;
}

// Classify a cell as one of:  "corner" | "edge" | "inner"
function classifyCell(r, c) {
  const isEdgeR = (r === 0 || r === SZ);
  const isEdgeC = (c === 0 || c === SZ);
  if (isEdgeR && isEdgeC) return 'corner';
  if (isEdgeR || isEdgeC) return 'edge';
  return 'inner';
}

// ---- WASM / engine interface ------------------------------------------------

// Allocate a persistent buffer in the WASM heap for cell arrays.
function ensureWasmBuffer() {
  if (!cpuCellsPtr) {
    cpuCellsPtr = engine._malloc(BOARD_SIZE * BOARD_SIZE * 4);
  }
}

function syncCellsToWasm() {
  engine.HEAP32.set(cells, cpuCellsPtr >> 2);
}

function wasmGetBestMove(isCpuX) {
  ensureWasmBuffer();
  syncCellsToWasm();
  return engine.ccall('wasm_getBestMove', 'number',
    ['number', 'number', 'number', 'number'],
    [BOARD_SIZE, TARGET, cpuCellsPtr, isCpuX ? 1 : 0]);
}

function wasmCheckWin(playerIdx) {
  ensureWasmBuffer();
  syncCellsToWasm();
  return engine.ccall('wasm_checkWin', 'number',
    ['number', 'number', 'number', 'number'],
    [BOARD_SIZE, TARGET, cpuCellsPtr, playerIdx]) === 1;
}

function wasmIsDraw() {
  ensureWasmBuffer();
  syncCellsToWasm();
  return engine.ccall('wasm_isDraw', 'number',
    ['number', 'number'],
    [BOARD_SIZE, cpuCellsPtr]) === 1;
}

// ---- Opening book lookup ----------------------------------------------------

// Given the human's first-move cell index, return the computer's optimal
// response cell index using D₈ symmetry to map the cached canonical answer.
function openingBookResponse(humanCellIdx) {
  const [hr, hc] = cellRC(humanCellIdx);
  const cls = classifyCell(hr, hc);

  const entry = openingBook.computerSecond[cls]; // { canonicalHuman, response }
  const [canHR, canHC] = entry.canonicalHuman;
  const [resR, resC] = entry.response;

  // Find the D₈ transform T that maps canonical human cell → actual human cell
  const T = findTransform([canHR, canHC], [hr, hc]);
  if (!T) {
    console.warn('No D₈ transform found — falling back to live engine');
    return wasmGetBestMove(cpuPlayer === 1);
  }

  // Apply the same transform to the canonical response
  const [tr, tc] = T([resR, resC]);
  return cellIdx(tr, tc);
}

// ---- DOM helpers ------------------------------------------------------------

const boardEl = document.getElementById('board');
const statusBar = document.getElementById('statusBar');
const statusText = document.getElementById('statusText');
const sideSelect = document.getElementById('sideSelect');
const gameSection = document.getElementById('gameSection');
const humanLabel = document.getElementById('humanLabel');
const engineStatus = document.getElementById('engineStatus');

function setStatus(msg, cls = '') {
  statusText.textContent = msg;
  statusBar.className = 'status-bar' + (cls ? ' ' + cls : '');
}

function buildBoard() {
  boardEl.innerHTML = '';
  for (let i = 0; i < BOARD_SIZE * BOARD_SIZE; i++) {
    const cell = document.createElement('div');
    cell.className = 'cell hoverable';
    cell.dataset.idx = i;
    cell.setAttribute('role', 'gridcell');
    cell.setAttribute('aria-label', `Cell ${Math.floor(i / BOARD_SIZE) + 1}-${(i % BOARD_SIZE) + 1}`);
    cell.addEventListener('click', onCellClick);
    boardEl.appendChild(cell);
  }
}

function renderBoard(winLine = null) {
  const cellEls = boardEl.querySelectorAll('.cell');
  cellEls.forEach((el, i) => {
    const v = cells[i];
    el.className = 'cell';
    if (v === 1) { el.textContent = 'X'; el.classList.add('x'); }
    else if (v === 2) { el.textContent = 'O'; el.classList.add('o'); }
    else {
      el.textContent = '';
      if (!gameOver) el.classList.add('hoverable');
    }
    if (winLine && winLine.includes(i)) el.classList.add('winner');
  });
}

function markThinking() {
  const cellEls = boardEl.querySelectorAll('.cell.hoverable');
  cellEls.forEach(el => { el.classList.remove('hoverable'); el.classList.add('thinking'); });
}

function unmarkThinking() {
  const cellEls = boardEl.querySelectorAll('.cell.thinking');
  cellEls.forEach(el => { el.classList.remove('thinking'); if (!gameOver) el.classList.add('hoverable'); });
}

// ---- Win detection (also finds the winning line for highlighting) ----------

function findWinLine(playerIdx) {
  const pm = new Set();
  for (let i = 0; i < BOARD_SIZE * BOARD_SIZE; i++)
    if (cells[i] === playerIdx) pm.add(i);

  // Horizontal
  for (let r = 0; r < BOARD_SIZE; r++)
    for (let c = 0; c <= BOARD_SIZE - TARGET; c++) {
      const line = Array.from({ length: TARGET }, (_, k) => cellIdx(r, c + k));
      if (line.every(i => pm.has(i))) return line;
    }
  // Vertical
  for (let c = 0; c < BOARD_SIZE; c++)
    for (let r = 0; r <= BOARD_SIZE - TARGET; r++) {
      const line = Array.from({ length: TARGET }, (_, k) => cellIdx(r + k, c));
      if (line.every(i => pm.has(i))) return line;
    }
  // Diagonal ↘
  for (let r = 0; r <= BOARD_SIZE - TARGET; r++)
    for (let c = 0; c <= BOARD_SIZE - TARGET; c++) {
      const line = Array.from({ length: TARGET }, (_, k) => cellIdx(r + k, c + k));
      if (line.every(i => pm.has(i))) return line;
    }
  // Diagonal ↙
  for (let r = TARGET - 1; r < BOARD_SIZE; r++)
    for (let c = 0; c <= BOARD_SIZE - TARGET; c++) {
      const line = Array.from({ length: TARGET }, (_, k) => cellIdx(r - k, c + k));
      if (line.every(i => pm.has(i))) return line;
    }
  return null;
}

// ---- Game logic -------------------------------------------------------------

function applyMove(idx, player) {
  cells[idx] = player;
  moveNumber++;
}

function checkGameOver(lastPlayer) {
  const winLine = findWinLine(lastPlayer);
  if (winLine) {
    gameOver = true;
    renderBoard(winLine);
    const winner = lastPlayer === 1 ? 'X' : 'O';
    const human = lastPlayer === humanPlayer;
    setStatus(human ? `You win! 🎉` : `Computer wins`, lastPlayer === 1 ? 'x-wins' : 'o-wins');
    return true;
  }
  if (wasmIsDraw()) {
    gameOver = true;
    renderBoard();
    setStatus('Draw — perfect play!', 'draw');
    return true;
  }
  return false;
}

async function cpuTurn() {
  const isCpuX = cpuPlayer === 1;
  setStatus('Computer thinking…', 'cpu-turn');
  markThinking();

  // Small delay so the browser can paint the "thinking" state before blocking
  await new Promise(r => setTimeout(r, 30));

  let move;

  // ---- Opening book logic --------------------------------------------------
  if (moveNumber === 0 && isCpuX) {
    // Computer goes first: always play the cached optimal first move
    const { row, col } = openingBook.computerFirst;
    move = cellIdx(row, col);
  } else if (moveNumber === 1 && !isCpuX) {
    // Computer goes second: find the human's first move and look up response
    const humanFirstIdx = cells.findIndex((v, i) => v === humanPlayer);
    move = openingBookResponse(humanFirstIdx);
  } else {
    // All subsequent moves: live engine (TT is warm from previous searches)
    move = wasmGetBestMove(isCpuX);
  }

  unmarkThinking();
  applyMove(move, cpuPlayer);
  renderBoard();

  if (!checkGameOver(cpuPlayer)) {
    setStatus('Your turn', 'your-turn');
  }
}

function onCellClick(e) {
  if (gameOver || !engineReady) return;
  const idx = parseInt(e.currentTarget.dataset.idx);
  if (cells[idx] !== 0) return;

  // Human move
  applyMove(idx, humanPlayer);
  renderBoard();
  if (checkGameOver(humanPlayer)) return;

  // CPU responds
  setStatus('Computer thinking…', 'cpu-turn');
  setTimeout(() => cpuTurn(), 10); // allow render before blocking
}

function startGame(human) {
  humanPlayer = human;
  cpuPlayer = human === 1 ? 2 : 1;
  cells.fill(0);
  moveNumber = 0;
  gameOver = false;

  humanLabel.textContent = human === 1 ? 'X' : 'O';
  humanLabel.style.color = human === 1 ? 'var(--x-color)' : 'var(--o-color)';

  sideSelect.classList.add('hidden');
  gameSection.classList.remove('hidden');
  buildBoard();

  if (cpuPlayer === 1) {
    // Computer goes first as X
    setStatus('Computer thinking…', 'cpu-turn');
    setTimeout(() => cpuTurn(), 40);
  } else {
    setStatus('Your turn', 'your-turn');
  }
}

function restartGame() {
  gameSection.classList.add('hidden');
  sideSelect.classList.remove('hidden');
  humanLabel.textContent = '—';
  humanLabel.style.color = '';
  cells.fill(0);
  moveNumber = 0;
  gameOver = false;
}

// ---- Initialization ---------------------------------------------------------

document.getElementById('btnX').addEventListener('click', () => {
  if (engineReady) startGame(1);
});
document.getElementById('btnO').addEventListener('click', () => {
  if (engineReady) startGame(2);
});
document.getElementById('btnRestart').addEventListener('click', restartGame);

async function init() {
  try {
    // Load WASM module
    engine = await createEngineModule();
    engine.ccall('wasm_init', null, [], []);

    // Load opening book
    const resp = await fetch('public/opening_book.json');
    openingBook = await resp.json();

    engineReady = true;
    engineStatus.textContent = 'Engine ready';
    engineStatus.classList.add('ready');
  } catch (err) {
    console.error('Engine load failed:', err);
    engineStatus.textContent = 'Engine failed to load';
    engineStatus.classList.add('error');
  }
}

init();
