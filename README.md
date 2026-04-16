# TicTacToeCpp: High-Performance Minimax Solver

**TicTacToeCpp** is an aggressively optimized, multithreaded engine for solving
generalized Tic-Tac-Toe games (m,n,k-games). By employing a suite of advanced computer
science techniques — hardware-level bitwise operations, incremental Zobrist hashing,
$D_8$ symmetry reduction, correct alpha-beta bound classification, and bounded
parallelism — it achieves very high node evaluation throughput.

It was purpose-built to extract maximum performance from modern Apple Silicon
hardware, specifically optimized for the **Apple M2 Studio (12 CPU cores, 32 GB
Unified Memory)**.

---

## 🧠 Core Computer Science Theory

### 1. The Minimax Algorithm with Alpha-Beta Pruning

At its core, the engine uses **Minimax** — a recursive algorithm for zero-sum games that
assumes both players play optimally: the maximizing player picks the highest score, the
minimizing player picks the lowest.

**Alpha-Beta Pruning** is layered on top. It maintains two values:
- `α` — the minimum score the maximizing player is *guaranteed* regardless of what the
  minimizer does.
- `β` — the maximum score the minimizing player is *guaranteed* regardless of what the
  maximizer does.

If a branch's evaluation falls outside the `[α, β]` window — meaning it can never
influence the outcome — it is immediately abandoned ("pruned"), dramatically reducing the
search space without affecting correctness.

---

### 2. State Representation via Bitboards

Traditional 2D arrays are cache-unfriendly. The engine represents the entire board as a
**single `uint64_t`** integer using 2 bits per cell:

```
00 = Empty   01 = Player X   10 = Player O
```

A 5×5 board occupies exactly 50 of the available 64 bits. Extracting or placing a piece
is a single bit-shift and mask — operations that execute in one CPU cycle.

A **second pair of 1-bit-per-cell masks** (`xBits`, `oBits`) is maintained alongside
the 2-bit state so that win checking never needs to scan cells at all — see §4.

---

### 3. Incremental Zobrist Hashing (O(1) Canonical State)

**Zobrist hashing** is the standard technique in chess engines for converting board
positions into compact 64-bit keys. A precomputed lookup table
`zobristTable[cell][player]` stores a random `uint64_t` for each (cell, player) pair.
The board hash = XOR of the table values for every occupied piece.

This engine carries **eight** running Zobrist hashes simultaneously — one per $D_8$
symmetry — updated incrementally on every `setPiece()` call:

```cpp
for (int sym = 0; sym < 8; sym++) {
    int mapped = applySymmetry(sym, row, col, size);   // maps cell through symmetry
    zobristHashes[sym] ^= zobristTable[mapped][playerIdx - 1];
}
```

**Correctness key:** only one shared Zobrist table is used (no separate table per
symmetry). As a result, if board B2 is a $D_8$ transform of B1, then
`{B2.zobristHashes}` is a permutation of `{B1.zobristHashes}`. Their **minimum** is
therefore identical, giving the same canonical key — which is the whole point of symmetry
reduction.

`getCanonicalState()` is now **O(1)**: just `min` over 8 pre-maintained values, compared
to the previous O(n²) approach that called `mapState()` seven times and iterated all
cells each time.

---

### 4. $D_8$ Symmetry Reduction (~8× state space compression)

A square board is symmetric under the **dihedral group $D_8$**: 4 rotations × 2
reflections = 8 distinct orientations. Many game states are mathematically equivalent
under these transforms.

By collapsing all 8 equivalents to a single **canonical representative** (the minimum of
the 8 Zobrist hashes), the effective state space shrinks by up to 8×, allowing far more
of the game tree to be pruned via the transposition table.

---

### 5. Lockless Transposition Table with Correct Bound Types

Even after symmetry reduction, different move sequences can reach the same board state.
A **transposition table (TT)** caches evaluated positions to avoid re-computing them.

#### Single-Word Atomic Design (No TOCTOU Race)

The previous implementation stored the key and score in two separate atomic fields,
creating a [TOCTOU race](https://en.wikipedia.org/wiki/Time-of-check_to_time-of-use):
Thread A could write a new key while Thread B was reading the old score. This engine
packs the entire entry into a **single `std::atomic<uint64_t>`**:

```
bits  0–15 : score   (stored as int16_t, covers the full ±1000±depth range)
bits 16–17 : Bound   (0=INVALID, 1=EXACT, 2=LOWER, 3=UPPER)
bits 18–57 : tag     (upper 40 bits of canonical key — collision detection)
bits 58–63 : unused
```

A single `memory_order_relaxed` load reads key tag, bound type, and score
simultaneously. There is no window for a data race between key and score.

#### Correct Alpha-Beta Bound Classification

Standard alpha-beta returns three types of results that have distinct meaning:

| Bound Type | Meaning | When it occurs |
|---|---|---|
| `EXACT` | True minimax value | Node fully searched within `[α, β]` window |
| `LOWER` | True value ≥ stored | Beta cutoff — maximizer found a move too good for minimizer |
| `UPPER` | True value ≤ stored | Alpha cutoff — minimizer found a move too good for maximizer |

On **lookup**, the bounds correctly narrow the search window before recursing:
```cpp
if (bound == EXACT) return cachedScore;
if (bound == LOWER) alpha = max(alpha, cachedScore);
if (bound == UPPER) beta  = min(beta,  cachedScore);
if (alpha >= beta)  return cachedScore; // window collapsed — safe cutoff
```

On **store**, the result is classified by comparing `bestEval` against the original
`[α, β]` window captured before the search (fail-low → UPPER, fail-high → LOWER,
otherwise EXACT). Storing wrong bound types causes the engine to make suboptimal moves;
this classification is now correct.

#### Table Size: 128 MB

```
tableSize = 1 << 24 = 16,777,216 entries × 8 bytes = 128 MB
```

This is ~0.4% of the available 32 GB RAM — no memory pressure whatsoever. The previous
attempt at 1 billion entries would have required 16 GB and caused an immediate OOM kill
(exit code 137 / SIGKILL) before the first move was computed.

The table size is a power of 2, so the index is a cheap bitwise AND:
```cpp
size_t index = canonicalKey & (tableSize - 1);   // no modulo division
```

---

### 6. Center-First Move Ordering (Precomputed)

Alpha-Beta pruning cuts far more branches when the best moves are evaluated first.
Central squares participate in more rows, columns, and diagonals and are statistically
stronger, so the engine orders them first.

Previously this sort ran inside `getAvailableMoves()` on **every single recursive node**.
Now the ordering is computed **once in the constructor** and stored in `moveOrder`.
`getAvailableMoves()` just iterates `moveOrder` and filters out occupied cells — no sort
at search time.

---

## ⚡ Performance on the Apple M2 Studio (12 cores, 32 GB RAM)

### Thread-Level Parallelism — Bounded at `hardware_concurrency()`

The engine spawns one `std::async` thread per root branch. A 5×5 board has 25 opening
moves, but the M2 Studio has only 12 CPU cores (8 P-cores + 4 E-cores). Spawning 25
threads simultaneously on a 12-core machine gives no additional parallelism and incurs
significant OS scheduling overhead.

Now threads are **batched at `std::thread::hardware_concurrency()` (= 12)**:

```
Batch 1: up to 12 moves computed in parallel → all 12 cores saturated
Batch 2: remaining moves computed in parallel → cores re-saturated
```

Because the transposition table is **global and shared across all threads**, results
computed in Batch 1 immediately warm the TT for Batch 2, meaning later batches finish
significantly faster.

### Unified Memory Architecture

Apple Silicon integrates CPU and RAM on a single die with unified memory bandwidth
(400+ GB/s on M2 Max). Since the minimax recursion is heavily memory-bound —
continuously reading `uint64_t` keys and scores from the 128 MB TT — the M2's
high-bandwidth memory access removes what would otherwise be a major bottleneck.

### 32 GB RAM Headroom

The full memory footprint of the engine at runtime:
- Transposition Table: **128 MB** (fixed, allocated once)
- Per-thread stack frames: ≈ 1–2 MB × 12 threads ≈ **~20 MB**
- OS + runtime overhead: **~4–6 GB**

**Total headroom remaining: ~25 GB.** There is no risk of memory pressure, swap, or OOM.

---

## 🚀 Getting Started

### Prerequisites

- C++20-capable compiler (Apple Clang 14+, GCC 12+)
- `make`

### Build and Run

```bash
make
./tictactoe_cpp
```

Or compile manually:

```bash
clang++ -O3 -std=c++20 -march=native src/main.cpp -o tictactoe_cpp
./tictactoe_cpp
```

### Usage

```
Board Size?          → enter e.g. 3, 4, or 5
Target Win Condition? → enter e.g. 3 (standard), 4, or 5
```

The engine plays both sides to a perfect conclusion, printing each move, its minimax
score, and the time taken per move (including per-thread timing for boards ≥ 4×4).

---

## 📐 Architecture Summary

```
main.cpp
 └── Solver.hpp
      ├── TTEntry          (lockless packed atomic TT entry)
      ├── minimax()        (alpha-beta with correct EXACT/LOWER/UPPER TT bounds)
      └── getBestMove()    (batched async parallelism, ≤ hardware_concurrency threads)
 └── Bitboard.hpp
      ├── state            (2-bit-per-cell compact encoding)
      ├── xBits / oBits    (1-bit-per-cell masks for O(1) win detection)
      ├── zobristHashes[8] (incremental D₈ Zobrist for O(1) canonical state)
      └── moveOrder[]      (precomputed center-first ordering; no per-node sort)
```
