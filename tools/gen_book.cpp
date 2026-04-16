// ============================================================================
// gen_book.cpp — Opening book generator (native build, runs on M2 Studio)
//
// Computes the optimal computer response to every canonical first move on a
// 4×4 board with target=4.  By D₈ symmetry there are exactly 3 canonical
// human opening classes:
//
//   Corner  (4 cells): [0,0] [0,3] [3,0] [3,3]  — canonical rep: [0,0]
//   Edge    (8 cells): [0,1] [0,2] [1,0] [2,0]  — canonical rep: [0,1]
//                      [1,3] [2,3] [3,1] [3,2]
//   Inner   (4 cells): [1,1] [1,2] [2,1] [2,2]  — canonical rep: [1,1]
//
// Usage: ./gen_book > public/opening_book.json
// Takes ~2 seconds on an M2 Studio (same as full 4×4 game move 1+2).
// ============================================================================
#include "../engine/Bitboard.hpp"
#include "../engine/Solver.hpp"
#include <iostream>
#include <chrono>

static constexpr int BOARD      = 4;
static constexpr int WIN_TARGET = 4;

struct BookEntry {
    int humanRow, humanCol;   // canonical human opening
    int cpuRow,   cpuCol;     // optimal computer response
};

BookEntry computeEntry(Solver& solver, int humanRow, int humanCol, bool cpuIsX) {
    auto t0 = std::chrono::high_resolution_clock::now();

    Bitboard board(BOARD, WIN_TARGET);
    // Human move (human is the OPPOSITE player from cpu)
    int humanPlayer = cpuIsX ? 2 : 1;
    board.setPiece(humanRow, humanCol, humanPlayer);

    auto [move, score] = solver.getBestMove(board, cpuIsX);
    (void)score;

    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms = t1 - t0;

    std::cerr << "  human=[" << humanRow << "," << humanCol << "]"
              << " -> cpu=[" << move/BOARD << "," << move%BOARD << "]"
              << " (" << ms.count() << " ms)\n";

    return { humanRow, humanCol, move/BOARD, move%BOARD };
}

int main() {
    Solver solver;

    std::cerr << "=== Generating opening book for " << BOARD << "x" << BOARD
              << " target=" << WIN_TARGET << " ===\n\n";

    // ---- Computer goes FIRST (plays as X on empty board) --------------------
    std::cerr << "Computer goes first:\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    Bitboard empty(BOARD, WIN_TARGET);
    auto [firstMove, firstScore] = solver.getBestMove(empty, true /*isX*/);
    (void)firstScore;
    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms = t1 - t0;
    int cfRow = firstMove / BOARD, cfCol = firstMove % BOARD;
    std::cerr << "  -> [" << cfRow << "," << cfCol << "] (" << ms.count() << " ms)\n\n";

    // ---- Computer goes SECOND (plays as O, responds to human X) -------------
    // Canonical representatives: corner=[0,0], edge=[0,1], inner=[1,1]
    std::cerr << "Computer goes second (as O, human is X):\n";
    BookEntry corner = computeEntry(solver, 0, 0, false /*cpuIsX=O*/);
    BookEntry edge   = computeEntry(solver, 0, 1, false);
    BookEntry inner  = computeEntry(solver, 1, 1, false);

    // ---- Output JSON --------------------------------------------------------
    std::cout << "{\n";
    std::cout << "  \"boardSize\": " << BOARD << ",\n";
    std::cout << "  \"target\": "    << WIN_TARGET << ",\n";
    std::cout << "  \"computerFirst\": {\n";
    std::cout << "    \"row\": " << cfRow << ", \"col\": " << cfCol << "\n";
    std::cout << "  },\n";
    std::cout << "  \"computerSecond\": {\n";
    std::cout << "    \"corner\": {\n";
    std::cout << "      \"canonicalHuman\": [" << corner.humanRow << "," << corner.humanCol << "],\n";
    std::cout << "      \"response\":       [" << corner.cpuRow   << "," << corner.cpuCol   << "]\n";
    std::cout << "    },\n";
    std::cout << "    \"edge\": {\n";
    std::cout << "      \"canonicalHuman\": [" << edge.humanRow << "," << edge.humanCol << "],\n";
    std::cout << "      \"response\":       [" << edge.cpuRow   << "," << edge.cpuCol   << "]\n";
    std::cout << "    },\n";
    std::cout << "    \"inner\": {\n";
    std::cout << "      \"canonicalHuman\": [" << inner.humanRow << "," << inner.humanCol << "],\n";
    std::cout << "      \"response\":       [" << inner.cpuRow   << "," << inner.cpuCol   << "]\n";
    std::cout << "    }\n";
    std::cout << "  }\n";
    std::cout << "}\n";

    std::cerr << "\nDone. Pipe stdout to public/opening_book.json\n";
    return 0;
}
