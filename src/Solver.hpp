#pragma once
#include "Bitboard.hpp"
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <future>
#include <vector>
#include <utility>
#include <mutex>
#include <chrono>

class Solver {
public:
    int minimax(Bitboard board, int depth, int alpha, int beta, bool isMaximizing, std::unordered_map<uint64_t, int>& tt) {
        if (board.checkWin(1)) return 1000 - depth; // X wins
        if (board.checkWin(2)) return -1000 + depth; // O wins
        
        auto moves = board.getAvailableMoves();
        if (moves.empty()) return 0; // Draw

        uint64_t canonicalKey = board.getCanonicalState();
        auto it = tt.find(canonicalKey);
        if (it != tt.end()) {
            return it->second;
        }

        if (isMaximizing) {
            int maxEval = std::numeric_limits<int>::min();
            for (int m : moves) {
                Bitboard nextBoard = board;
                nextBoard.setPiece(m / board.size, m % board.size, 1);
                int eval = minimax(nextBoard, depth + 1, alpha, beta, false, tt);
                maxEval = std::max(maxEval, eval);
                alpha = std::max(alpha, eval);
                if (beta <= alpha) break; // Beta cutoff
            }
            tt[canonicalKey] = maxEval;
            return maxEval;
        } else {
            int minEval = std::numeric_limits<int>::max();
            for (int m : moves) {
                Bitboard nextBoard = board;
                nextBoard.setPiece(m / board.size, m % board.size, 2);
                int eval = minimax(nextBoard, depth + 1, alpha, beta, true, tt);
                minEval = std::min(minEval, eval);
                beta = std::min(beta, eval);
                if (beta <= alpha) break; // Alpha cutoff
            }
            tt[canonicalKey] = minEval;
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
                std::unordered_map<uint64_t, int> localTT; 
                Bitboard nextBoard = board;
                nextBoard.setPiece(m / board.size, m % board.size, isX ? 1 : 2);
                
                auto t_start = std::chrono::high_resolution_clock::now();
                int eval = minimax(nextBoard, 0, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), !isX, localTT);
                auto t_end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> ms = t_end - t_start;

                if (board.size == 5) {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "  -> Thread finished branch [" << m / board.size << "," << m % board.size 
                              << "] | Score: " << eval << " | Cache Size: " << localTT.size() 
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
