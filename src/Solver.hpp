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

struct TTEntry {
    std::atomic<uint64_t> key{0};
    std::atomic<int> score{-9999};
};

class Solver {
public:
    TTEntry* table;
    size_t tableSize = 1000000000; // 1 Billion Entries (~16 GB of pure Hash Map power)

    Solver() {
        table = new TTEntry[tableSize];
    }

    ~Solver() {
        delete[] table;
    }

    int minimax(Bitboard board, int depth, int alpha, int beta, bool isMaximizing) {
        if (board.checkWin(1)) return 1000 - depth; // X wins
        if (board.checkWin(2)) return -1000 + depth; // O wins
        
        auto moves = board.getAvailableMoves();
        if (moves.empty()) return 0; // Draw

        uint64_t canonicalKey = board.getCanonicalState();
        size_t index = canonicalKey % tableSize;

        // --- GLOBAL LOCKLESS CACHE LOOKUP ---
        uint64_t cachedKey = table[index].key.load(std::memory_order_relaxed);
        if (cachedKey == canonicalKey) {
            int cachedScore = table[index].score.load(std::memory_order_relaxed);
            if (cachedScore != -9999) { // Avoid false hits on raw default values
                return cachedScore;
            }
        }

        if (isMaximizing) {
            int maxEval = std::numeric_limits<int>::min();
            for (int m : moves) {
                Bitboard nextBoard = board;
                nextBoard.setPiece(m / board.size, m % board.size, 1);
                int eval = minimax(nextBoard, depth + 1, alpha, beta, false);
                maxEval = std::max(maxEval, eval);
                alpha = std::max(alpha, eval);
                if (beta <= alpha) break; // Beta cutoff
            }
            
            // --- GLOBAL LOCKLESS CACHE WRITE OVERWRITE ---
            table[index].score.store(maxEval, std::memory_order_relaxed);
            table[index].key.store(canonicalKey, std::memory_order_relaxed);
            return maxEval;
            
        } else {
            int minEval = std::numeric_limits<int>::max();
            for (int m : moves) {
                Bitboard nextBoard = board;
                nextBoard.setPiece(m / board.size, m % board.size, 2);
                int eval = minimax(nextBoard, depth + 1, alpha, beta, true);
                minEval = std::min(minEval, eval);
                beta = std::min(beta, eval);
                if (beta <= alpha) break; // Alpha cutoff
            }
            
            // --- GLOBAL LOCKLESS CACHE WRITE OVERWRITE ---
            table[index].score.store(minEval, std::memory_order_relaxed);
            table[index].key.store(canonicalKey, std::memory_order_relaxed);
            return minEval;
        }
    }

    std::pair<int, int> getBestMove(Bitboard board, bool isX) {
        auto moves = board.getAvailableMoves();
        if (moves.empty()) return {-1, 0};

        std::vector<std::future<std::pair<int, int>>> futures;
        std::mutex cout_mutex;

        for (int m : moves) {
            futures.push_back(std::async(std::launch::async, [this, board, m, isX, &cout_mutex]() {
                if (board.size == 5) {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "  -> Thread starting branch [" << m / board.size << "," << m % board.size << "]...\n";
                }
                
                Bitboard nextBoard = board;
                nextBoard.setPiece(m / board.size, m % board.size, isX ? 1 : 2);
                
                auto t_start = std::chrono::high_resolution_clock::now();
                
                // We no longer pass localTT downward! It inherently calls the global `table` directly lock-free!
                int eval = minimax(nextBoard, 0, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), !isX);
                
                auto t_end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> ms = t_end - t_start;

                if (board.size == 5) {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "  -> Thread finished branch [" << m / board.size << "," << m % board.size 
                              << "] | Score: " << eval 
                              << " | Thread Time: " << ms.count() << " ms\n";
                }

                return std::make_pair(m, eval);
            }));
        }

        int bestMove = -1;
        int bestScore = isX ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
        if (isX) {
            for (auto& f : futures) {
                auto result = f.get();
                if (result.second > bestScore) {
                    bestScore = result.second;
                    bestMove = result.first;
                }
            }
        } else {
            for (auto& f : futures) {
                auto result = f.get();
                if (result.second < bestScore) {
                    bestScore = result.second;
                    bestMove = result.first;
                }
            }
        }
        return {bestMove, bestScore};
    }
};
