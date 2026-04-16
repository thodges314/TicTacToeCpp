#pragma once
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>

// ============================================================================
// Bitboard — compact game state representation
//
// Layout A: `state` — 2 bits per cell (for display / move generation)
//   00 = Empty, 01 = Player 1 (X), 10 = Player 2 (O)
//
// Layout B: `xBits` / `oBits` — 1 bit per cell (for win detection)
//   Maintained incrementally in setPiece(); no full scan needed in checkWin().
//
// Layout C: `zobristHashes[8]` — one Zobrist hash per D₈ symmetry
//   Maintained incrementally in setPiece(); getCanonicalState() is O(1).
// ============================================================================
class Bitboard {
public:
    uint64_t state  = 0;
    uint64_t xBits  = 0;   // bit i set iff X occupies cell i (1-bit-per-cell mask)
    uint64_t oBits  = 0;   // bit i set iff O occupies cell i
    int size;
    int target;

    // One Zobrist hash per D₈ symmetry (identity, 3 rotations, 4 reflections).
    // All updated incrementally in setPiece(); getCanonicalState() = min of these 8.
    uint64_t zobristHashes[8] = {};

    // Precomputed center-first move index ordering (computed once in constructor).
    std::vector<int> moveOrder;

    // ---- Static Zobrist table (single table, shared across all symmetries) ----
    // zobristTable[cellIdx][player-1] — a single random uint64 per (cell, player).
    // Using one table (not per-symmetry) is CRITICAL for correctness:
    // two boards related by symmetry must share the same 8-element hash set.
    static uint64_t zobristTable[25][2];
    static bool zobristInitialized;

    static void initZobrist() {
        if (zobristInitialized) return;
        std::mt19937_64 rng(0xDEADBEEFCAFE1234ULL); // fixed seed for reproducibility
        for (int cell = 0; cell < 25; cell++)
            for (int p = 0; p < 2; p++)
                zobristTable[cell][p] = rng();
        zobristInitialized = true;
    }

    // Maps cell (r, c) on a board of size `sz` through one of the 8 D₈ symmetries
    // and returns the resulting flat cell index.  Used during both setPiece (O(1)
    // incremental update) and the legacy mapState path.
    static int applySymmetry(int sym, int r, int c, int sz) {
        switch (sym) {
            case 0: return r * sz + c;                          // identity
            case 1: return c * sz + (sz - 1 - r);              // 90° clockwise
            case 2: return (sz-1-r) * sz + (sz-1-c);           // 180°
            case 3: return (sz-1-c) * sz + r;                  // 270° clockwise
            case 4: return r * sz + (sz-1-c);                  // horizontal flip
            case 5: return (sz-1-r) * sz + c;                  // vertical flip
            case 6: return c * sz + r;                          // main-diagonal flip
            case 7: return (sz-1-c) * sz + (sz-1-r);           // anti-diagonal flip
            default: return -1;
        }
    }

    // ---- Constructors --------------------------------------------------------

    Bitboard(int s, int t) : size(s), target(t) {
        initZobrist();
        buildMoveOrder();
    }

    // Reconstruct full derived state from a raw `state` word.
    Bitboard(uint64_t s, int sz, int t) : state(s), size(sz), target(t) {
        initZobrist();
        rebuildDerived();
        buildMoveOrder();
    }

    // ---- Piece operations ---------------------------------------------------

    void setPiece(int row, int col, int playerIdx) {
        int shift    = (row * size + col) * 2;
        int cellIdx  = row * size + col;

        // Update compact 2-bit state
        state |= (static_cast<uint64_t>(playerIdx) << shift);

        // Update 1-bit-per-cell masks (eliminates O(n) scan in checkWin)
        if (playerIdx == 1) xBits |= (1ULL << cellIdx);
        else                oBits |= (1ULL << cellIdx);

        // Update all 8 Zobrist hashes in O(1) — each uses the single shared table
        // but looks up the cell index AFTER applying that symmetry's transform.
        for (int sym = 0; sym < 8; sym++) {
            int mapped = applySymmetry(sym, row, col, size);
            zobristHashes[sym] ^= zobristTable[mapped][playerIdx - 1];
        }
    }

    int getPiece(int row, int col) const {
        int shift = (row * size + col) * 2;
        return (state >> shift) & 0b11;
    }

    bool isEmpty(int row, int col) const { return getPiece(row, col) == 0; }

    // ---- Canonical state (D₈ symmetry reduction) ----------------------------
    //
    // O(1): returns the minimum of the 8 incrementally maintained Zobrist hashes.
    //
    // Correctness: if board B2 is a D₈ transform of B1 then {B2.zobristHashes}
    // is a permutation of {B1.zobristHashes} (their unordered sets are identical),
    // so min(B1.zobristHashes) == min(B2.zobristHashes).  This holds because the
    // Zobrist table is a single shared lookup — see initZobrist() note above.
    uint64_t getCanonicalState() const {
        uint64_t minHash = zobristHashes[0];
        for (int i = 1; i < 8; i++)
            if (zobristHashes[i] < minHash) minHash = zobristHashes[i];
        return minHash;
    }

    // ---- Move generation ----------------------------------------------------
    //
    // Iterates `moveOrder` (precomputed center-first) and returns only empty cells.
    // No per-call sort — the ordering is done once in the constructor.
    std::vector<int> getAvailableMoves() const {
        std::vector<int> moves;
        moves.reserve(size * size);
        for (int m : moveOrder)
            if (((state >> (m * 2)) & 0b11) == 0)
                moves.push_back(m);
        return moves;
    }

    // ---- Win detection ------------------------------------------------------
    //
    // Uses the prebuilt `xBits` / `oBits` masks — no full-cell iteration needed
    // to build playerMask.  All win patterns are bitwise AND against the mask.
    bool checkWin(int playerIdx) const {
        const uint64_t playerMask = (playerIdx == 1) ? xBits : oBits;

        // Horizontal runs
        for (int r = 0; r < size; r++)
            for (int c = 0; c <= size - target; c++) {
                uint64_t mask = ((1ULL << target) - 1) << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }

        // Vertical runs
        uint64_t vTemplate = 0;
        for (int i = 0; i < target; i++) vTemplate |= (1ULL << (i * size));
        for (int c = 0; c < size; c++)
            for (int r = 0; r <= size - target; r++) {
                uint64_t mask = vTemplate << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }

        // Diagonal (top-left → bottom-right)
        uint64_t d1Template = 0;
        for (int i = 0; i < target; i++) d1Template |= (1ULL << (i * (size + 1)));
        for (int r = 0; r <= size - target; r++)
            for (int c = 0; c <= size - target; c++) {
                uint64_t mask = d1Template << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }

        // Diagonal (top-right → bottom-left)
        uint64_t d2Template = 0;
        for (int i = 0; i < target; i++) d2Template |= (1ULL << (i * (size - 1)));
        for (int r = target - 1; r < size; r++)
            for (int c = 0; c <= size - target; c++) {
                uint64_t mask = d2Template << (r * size + c);
                if ((playerMask & mask) == mask) return true;
            }

        return false;
    }

    // ---- Display ------------------------------------------------------------

    void display() const {
        for (int r = 0; r < size; r++) {
            for (int c = 0; c < size; c++) {
                int p = getPiece(r, c);
                if      (p == 1) std::cout << "X ";
                else if (p == 2) std::cout << "O ";
                else             std::cout << ". ";
            }
            std::cout << "\n";
        }
    }

private:
    // Precompute center-first move ordering once; getAvailableMoves() uses it
    // directly without sorting on every node.
    void buildMoveOrder() {
        moveOrder.resize(size * size);
        std::iota(moveOrder.begin(), moveOrder.end(), 0);
        float center = (size - 1) / 2.0f;
        std::sort(moveOrder.begin(), moveOrder.end(), [&](int a, int b) {
            float da = static_cast<float>((a/size) - center) * ((a/size) - center)
                     + static_cast<float>((a%size) - center) * ((a%size) - center);
            float db = static_cast<float>((b/size) - center) * ((b/size) - center)
                     + static_cast<float>((b%size) - center) * ((b%size) - center);
            return da < db;
        });
    }

    // Rebuild xBits, oBits, and zobristHashes from an existing `state` word.
    void rebuildDerived() {
        xBits = 0; oBits = 0;
        for (int i = 0; i < 8; i++) zobristHashes[i] = 0;
        for (int i = 0; i < size * size; i++) {
            int p = (state >> (i * 2)) & 0b11;
            if (p == 0) continue;
            int r = i / size, c = i % size;
            if (p == 1) xBits |= (1ULL << i);
            else        oBits |= (1ULL << i);
            for (int sym = 0; sym < 8; sym++) {
                int mapped = applySymmetry(sym, r, c, size);
                zobristHashes[sym] ^= zobristTable[mapped][p - 1];
            }
        }
    }
};

// C++17 inline static definitions (header-only, #pragma once prevents ODR conflicts)
inline uint64_t Bitboard::zobristTable[25][2] = {};
inline bool     Bitboard::zobristInitialized   = false;
