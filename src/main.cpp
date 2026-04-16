#include <iostream>
#include <chrono>
#include "Bitboard.hpp"
#include "Solver.hpp"

int main() {
    std::cout << "--- C++ OPTIMIZED BITBOARD TTT ---\n";
    int size;
    int target;
    std::cout << "Board Size? ";
    std::cin >> size;
    std::cout << "Target Win Condition? ";
    std::cin >> target;
    
    Bitboard board(size, target);
    Solver solver;
    bool xTurn = true;

    while (true) {
        if (board.getAvailableMoves().empty()) {
            std::cout << "Draw!\n";
            break;
        }

        auto start = std::chrono::high_resolution_clock::now();
        auto result = solver.getBestMove(board, xTurn);
        auto end = std::chrono::high_resolution_clock::now();
        
        int bestMove = result.first;
        int bestScore = result.second;
        std::chrono::duration<double, std::milli> elapsed = end - start;

        int playerIdx = xTurn ? 1 : 2;
        board.setPiece(bestMove / size, bestMove % size, playerIdx);
        
        std::cout << "\nPlayer " << (xTurn ? "X" : "O") << " plays: [" 
                  << bestMove / size << "," << bestMove % size << "] | Score: " << bestScore << " | Time: " 
                  << elapsed.count() << " ms\n";
        
        board.display();

        if (board.checkWin(playerIdx)) {
            std::cout << "PLAYER " << (xTurn ? "X" : "O") << " WINS!\n";
            break;
        }

        xTurn = !xTurn;
    }

    return 0;
}
