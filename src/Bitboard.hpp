#pragma once
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>

// 2 bits per cell
// 00 = Empty, 01 = Player 1 (X), 10 = Player 2 (O)
class Bitboard {
public:
    uint64_t state;
    int size;
    int target;

    Bitboard(int s, int t) : state(0), size(s), target(t) {}
    Bitboard(uint64_t s, int sz, int t) : state(s), size(sz), target(t) {}

    void setPiece(int row, int col, int playerIdx) {
        int shift = (row * size + col) * 2;
        state |= (static_cast<uint64_t>(playerIdx) << shift);
    }

    int getPiece(int row, int col) const {
        int shift = (row * size + col) * 2;
        return (state >> shift) & 0b11;
    }

    bool isEmpty(int row, int col) const {
        return getPiece(row, col) == 0;
    }

    uint64_t mapState(int (*mapFunc)(int, int, int), int sz) const {
        uint64_t newState = 0;
        for (int r = 0; r < sz; r++) {
            for (int c = 0; c < sz; c++) {
                int p = getPiece(r, c);
                if (p != 0) {
                    int newIdx = mapFunc(r, c, sz);
                    newState |= (static_cast<uint64_t>(p) << (newIdx * 2));
                }
            }
        }
        return newState;
    }

    uint64_t getCanonicalState() const {
        uint64_t minState = state;
        int sz = size;
        
        auto r90 = [](int r, int c, int sz) -> int { return c * sz + (sz - 1 - r); };
        auto r180 = [](int r, int c, int sz) -> int { return (sz - 1 - r) * sz + (sz - 1 - c); };
        auto r270 = [](int r, int c, int sz) -> int { return (sz - 1 - c) * sz + r; };
        auto fh = [](int r, int c, int sz) -> int { return r * sz + (sz - 1 - c); };
        auto fv = [](int r, int c, int sz) -> int { return (sz - 1 - r) * sz + c; };
        auto fd1 = [](int r, int c, int sz) -> int { return c * sz + r; };
        auto fd2 = [](int r, int c, int sz) -> int { return (sz - 1 - c) * sz + (sz - 1 - r); };

        uint64_t states[7] = {
            mapState(r90, sz), mapState(r180, sz), mapState(r270, sz),
            mapState(fh, sz), mapState(fv, sz), mapState(fd1, sz), mapState(fd2, sz)
        };

        for (int i = 0; i < 7; i++) {
            if (states[i] < minState) {
                minState = states[i];
            }
        }
        return minState;
    }

    std::vector<int> getAvailableMoves() const {
        std::vector<int> moves;
        moves.reserve(size * size);
        for (int i = 0; i < size * size; i++) {
            if (((state >> (i * 2)) & 0b11) == 0) {
                moves.push_back(i);
            }
        }

        float center = (size - 1) / 2.0f;
        std::sort(moves.begin(), moves.end(), [this, center](int a, int b) {
            int ra = a / size; int ca = a % size;
            int rb = b / size; int cb = b % size;
            float distA = (ra - center) * (ra - center) + (ca - center) * (ca - center);
            float distB = (rb - center) * (rb - center) + (cb - center) * (cb - center);
            return distA < distB;
        });

        return moves;
    }

    bool checkWin(int playerIdx) const {
        uint64_t playerMask = 0;
        for (int i = 0; i < size * size; i++) {
            if (getPiece(i / size, i % size) == playerIdx) {
                playerMask |= (1ULL << i);
            }
        }

        for (int r = 0; r < size; r++) {
            for (int c = 0; c <= size - target; c++) {
                uint64_t mask = ((1ULL << target) - 1) << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }
        }

        uint64_t vMaskTemplate = 0;
        for (int i = 0; i < target; i++) vMaskTemplate |= (1ULL << (i * size));
        for (int c = 0; c < size; c++) {
            for (int r = 0; r <= size - target; r++) {
                uint64_t mask = vMaskTemplate << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }
        }

        uint64_t d1MaskTemplate = 0;
        for (int i = 0; i < target; i++) d1MaskTemplate |= (1ULL << (i * (size + 1)));
        for (int r = 0; r <= size - target; r++) {
            for (int c = 0; c <= size - target; c++) {
                uint64_t mask = d1MaskTemplate << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }
        }

        uint64_t d2MaskTemplate = 0;
        for (int i = 0; i < target; i++) d2MaskTemplate |= (1ULL << (i * (size - 1)));
        for (int r = target - 1; r < size; r++) {
            for (int c = 0; c <= size - target; c++) {
                uint64_t mask = d2MaskTemplate << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }
        }

        return false;
    }

    void display() const {
        for (int r = 0; r < size; r++) {
            for (int c = 0; c < size; c++) {
                int p = getPiece(r, c);
                if (p == 1) std::cout << "X ";
                else if (p == 2) std::cout << "O ";
                else std::cout << ". ";
            }
            std::cout << "\n";
        }
    }
};
