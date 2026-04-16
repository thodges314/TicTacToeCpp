#pragma once
#include "Bitboard.hpp"
#include <algorithm>
#include <limits>
#include <future>
#include <vector>
#include <utility>
#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <iostream>

// ============================================================================
// Transposition Table — lockless, single-atomic-word design
//
// Each entry is a single std::atomic<uint64_t> packed as:
//
//   bits  0–15 : score  — stored as int16_t cast to uint16_t
//   bits 16–17 : Bound  — 0=INVALID (default/zero-init), 1=EXACT, 2=LOWER, 3=UPPER
//   bits 18–57 : tag    — upper 40 bits of the canonical key (key >> 24)
//   bits 58–63 : unused (always 0)
//
// Because the entire entry is a single 64-bit atomic load/store, there is no
// TOCTOU race between reading the key tag and reading the score.  Both are
// verified atomically in a single operation.
//
// tableSize = 1 << 24 (16,777,216 entries × 8 bytes = **128 MB**)
// Index = key & (tableSize - 1)  [fast bitwise AND since tableSize is power-of-2]
// Tag   = key >> 24              [upper 40 bits, not used by index]
//
// Bound type semantics (standard alpha-beta conventions):
//   EXACT : stored value == true minimax value (fully searched node)
//   LOWER : true value >= stored value (beta cutoff; maximizer found a very good move)
//   UPPER : true value <= stored value (alpha cutoff; minimizer found a very good move)
// ============================================================================

enum class Bound : uint8_t { INVALID = 0, EXACT = 1, LOWER = 2, UPPER = 3 };

struct TTEntry {
    std::atomic<uint64_t> data{0}; // zero-init → Bound::INVALID for all entries

    static uint64_t pack(uint64_t key, int score, Bound bound) {
        uint64_t tag   = key >> 24;                          // upper 40 bits
        uint64_t sc    = static_cast<uint64_t>(static_cast<uint16_t>(static_cast<int16_t>(score)));
        uint64_t bd    = static_cast<uint64_t>(static_cast<uint8_t>(bound));
        return (tag << 18) | (bd << 16) | sc;
    }

    void store(uint64_t key, int score, Bound bound) {
        data.store(pack(key, score, bound), std::memory_order_relaxed);
    }

    // Returns true and fills `score`/`bound` if the entry is valid and the key matches.
    bool lookup(uint64_t key, int& score, Bound& bound) const {
        uint64_t raw = data.load(std::memory_order_relaxed);
        Bound b = static_cast<Bound>((raw >> 16) & 0x3);
        if (b == Bound::INVALID)       return false; // uninitialized slot
        if ((raw >> 18) != (key >> 24)) return false; // tag mismatch — collision
        score = static_cast<int>(static_cast<int16_t>(raw & 0xFFFF));
        bound = b;
        return true;
    }
};

// ============================================================================
// Solver
// ============================================================================
class Solver {
public:
    // 16 M entries × 8 bytes = 128 MB — comfortably within 32 GB, no OOM risk.
    // Must remain a power of 2 so index = key & (tableSize-1) is a cheap bitwise AND.
    static constexpr size_t tableSize = 1u << 24; // 16,777,216

    TTEntry* table;

    Solver()  { table = new TTEntry[tableSize]; } // zero-inits → all INVALID
    ~Solver() { delete[] table; }

    // ---- Minimax with Alpha-Beta Pruning, TT lookup, and depth cap -----------
    //
    // Bound types are classified and stored correctly:
    //   bestEval <= originalAlpha  →  UPPER  (failed low;  true value ≤ bestEval)
    //   bestEval >= originalBeta   →  LOWER  (failed high; true value ≥ bestEval)
    //   otherwise                  →  EXACT
    //
    // maxDepth: when depth == maxDepth and no terminal state has been reached,
    // returns 0 (draw heuristic). Set to INT_MAX for a full perfect solve.
    int minimax(Bitboard board, int depth, int alpha, int beta, bool isMaximizing,
                int maxDepth = std::numeric_limits<int>::max()) {
        if (board.checkWin(1)) return  1000 - depth; // X wins
        if (board.checkWin(2)) return -1000 + depth; // O wins

        auto moves = board.getAvailableMoves();
        if (moves.empty()) return 0; // draw

        // Depth cap: treat as a draw if we've hit the search limit
        if (depth >= maxDepth) return 0;

        uint64_t canonicalKey = board.getCanonicalState();           // O(1)
        size_t   index        = canonicalKey & (tableSize - 1);      // power-of-2 modulo

        const int originalAlpha = alpha;
        const int originalBeta  = beta;

        // --- TT lookup with proper bound narrowing ---------------------------
        {
            int   cachedScore;
            Bound cachedBound;
            if (table[index].lookup(canonicalKey, cachedScore, cachedBound)) {
                if (cachedBound == Bound::EXACT)
                    return cachedScore;
                if (cachedBound == Bound::LOWER)
                    alpha = std::max(alpha, cachedScore);
                if (cachedBound == Bound::UPPER)
                    beta  = std::min(beta,  cachedScore);
                if (alpha >= beta)
                    return cachedScore; // window collapsed; cut off
            }
        }

        // --- Alpha-Beta search -----------------------------------------------
        int bestEval;
        if (isMaximizing) {
            bestEval = std::numeric_limits<int>::min();
            for (int m : moves) {
                Bitboard next = board;
                next.setPiece(m / board.size, m % board.size, 1);
                int eval = minimax(next, depth + 1, alpha, beta, false, maxDepth);
                bestEval = std::max(bestEval, eval);
                alpha    = std::max(alpha,    eval);
                if (beta <= alpha) break; // beta cutoff
            }
        } else {
            bestEval = std::numeric_limits<int>::max();
            for (int m : moves) {
                Bitboard next = board;
                next.setPiece(m / board.size, m % board.size, 2);
                int eval = minimax(next, depth + 1, alpha, beta, true, maxDepth);
                bestEval = std::min(bestEval, eval);
                beta     = std::min(beta,     eval);
                if (beta <= alpha) break; // alpha cutoff
            }
        }

        // --- Classify result and write to TT ---------------------------------
        Bound bound;
        if      (bestEval <= originalAlpha) bound = Bound::UPPER;
        else if (bestEval >= originalBeta)  bound = Bound::LOWER;
        else                                bound = Bound::EXACT;

        table[index].store(canonicalKey, bestEval, bound);

        return bestEval;
    }

    // ---- Top-level move selection -------------------------------------------
    //
    // Dispatches root branches to std::async threads, but caps concurrency at
    // hardware_concurrency() (12 on an M2 Studio).  Processing in size-12 batches
    // prevents thread-thrashing and ensures the OS scheduler is never overloaded.
    // Because all threads share the global TT, later batches benefit from cache
    // hits produced by earlier batches — a meaningful speedup for 5×5 boards.
    std::pair<int, int> getBestMove(Bitboard board, bool isX,
                                    int maxDepth = std::numeric_limits<int>::max()) {
        auto moves = board.getAvailableMoves();
        if (moves.empty()) return {-1, 0};

        std::mutex  cout_mutex;
        const unsigned int maxThreads = []() {
            unsigned int hw = std::thread::hardware_concurrency();
            return (hw > 0) ? hw : 12u; // safe fallback
        }();

        int bestMove  = -1;
        int bestScore = isX ? std::numeric_limits<int>::min()
                             : std::numeric_limits<int>::max();

        // Process moves in batches of at most maxThreads (= 12 on M2 Studio)
        for (size_t i = 0; i < moves.size(); i += maxThreads) {
            size_t batchEnd = std::min(i + static_cast<size_t>(maxThreads), moves.size());
            std::vector<std::future<std::pair<int,int>>> futures;
            futures.reserve(batchEnd - i);

            for (size_t j = i; j < batchEnd; j++) {
                int m = moves[j];
                futures.push_back(std::async(std::launch::async,
                        [this, board, m, isX, maxDepth, &cout_mutex]() -> std::pair<int,int> {
                        if (board.size >= 4) {
                            std::lock_guard<std::mutex> lk(cout_mutex);
                            std::cout << "  -> Thread starting branch ["
                                      << m / board.size << "," << m % board.size << "]...\n";
                        }
                        Bitboard next = board;
                        next.setPiece(m / board.size, m % board.size, isX ? 1 : 2);

                        auto t0  = std::chrono::high_resolution_clock::now();
                        int eval = minimax(next, 0,
                                          std::numeric_limits<int>::min(),
                                          std::numeric_limits<int>::max(),
                                          !isX, maxDepth);
                        auto t1  = std::chrono::high_resolution_clock::now();
                        std::chrono::duration<double, std::milli> ms = t1 - t0;

                        if (board.size >= 4) {
                            std::lock_guard<std::mutex> lk(cout_mutex);
                            std::cout << "  -> Thread finished  branch ["
                                      << m / board.size << "," << m % board.size
                                      << "] | Score: " << eval
                                      << " | Thread Time: " << ms.count() << " ms\n";
                        }
                        return {m, eval};
                    }
                ));
            }

            // Collect this batch before launching the next
            for (auto& f : futures) {
                auto [move, score] = f.get();
                if ( isX && score > bestScore) { bestScore = score; bestMove = move; }
                if (!isX && score < bestScore) { bestScore = score; bestMove = move; }
            }
        }

        return {bestMove, bestScore};
    }
};
