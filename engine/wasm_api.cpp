// ============================================================================
// wasm_api.cpp — Emscripten bridge between JavaScript and the C++ engine
//
// Exported functions (callable from JS via cwrap):
//   wasm_init()          — call once after module loads; allocates the TT
//   wasm_getBestMove()   — returns best move as flat cell index (row*size+col)
//   wasm_checkWin()      — returns 0=no win, 1=X wins, 2=O wins
//   wasm_isDraw()        — returns 1 if board is full with no winner
//
// Board state is passed as a flat int array (0=empty, 1=X, 2=O) to avoid
// BigInt boundary issues with uint64_t.  The Bitboard is reconstructed from
// the cell array on each call; the global TT persists across calls.
// ============================================================================
#include "engine/Solver.hpp"
#include <emscripten/emscripten.h>
#include <cstring>

static Solver* g_solver = nullptr;

extern "C" {

// Call once after the WASM module loads.
EMSCRIPTEN_KEEPALIVE
void wasm_init() {
    if (!g_solver) g_solver = new Solver();
}

// Returns the best move as a flat index (row * boardSize + col).
// Returns -1 if there are no available moves.
// cells: pointer to int[boardSize*boardSize], values 0/1/2
// isX:   1 if the CPU is playing as X, 0 if playing as O
EMSCRIPTEN_KEEPALIVE
int wasm_getBestMove(int boardSize, int target, int* cells, int isX) {
    Bitboard board(boardSize, target);
    for (int i = 0; i < boardSize * boardSize; i++) {
        if (cells[i] != 0) {
            board.setPiece(i / boardSize, i % boardSize, cells[i]);
        }
    }
    auto [move, score] = g_solver->getBestMoveSingleThreaded(board, isX == 1);
    (void)score;
    return move;
}

// Returns 1 if playerIdx (1=X, 2=O) has won, 0 otherwise.
EMSCRIPTEN_KEEPALIVE
int wasm_checkWin(int boardSize, int target, int* cells, int playerIdx) {
    Bitboard board(boardSize, target);
    for (int i = 0; i < boardSize * boardSize; i++) {
        if (cells[i] != 0) {
            board.setPiece(i / boardSize, i % boardSize, cells[i]);
        }
    }
    return board.checkWin(playerIdx) ? 1 : 0;
}

// Returns 1 if the board is full (draw), 0 otherwise.
EMSCRIPTEN_KEEPALIVE
int wasm_isDraw(int boardSize, int* cells) {
    for (int i = 0; i < boardSize * boardSize; i++) {
        if (cells[i] == 0) return 0;
    }
    return 1;
}

} // extern "C"
