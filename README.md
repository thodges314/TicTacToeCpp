# TicTacToeCpp: High-Performance Minimax Solver

**TicTacToeCpp** is an aggressively optimized, multithreaded engine for solving generalized Tic-Tac-Toe games (m, n, k-games). By employing a suite of advanced computer science paradigms—hardware-level bitwise operations, heuristic move ordering, game tree reduction via mathematical symmetries, and modern parallelism—this engine achieves remarkable node evaluation throughput.

It was purpose-built to extract maximum performance from modern Apple Silicon hardware, specifically optimized for architectures like the **Apple M2 Studio (with 32 GB of RAM)**.

---

## 🧠 Core Computer Science Theories & Architecture

Solving a board game optimally requires exploring its "game tree" using the **Minimax algorithm**. However, the number of possible states in Tic-Tac-Toe variations explodes exponentially (e.g., standard 3x3 has 362,880 possible games, expanding massively for 4x4 or 5x5). To combat this combinatorial explosion, this engine employs several critical optimizations:

### 1. The Minimax Algorithm with Alpha-Beta Pruning
At its core, the engine uses Minimax. A recursive algorithm used in zero-sum games, Minimax assumes both players play optimally: maximizing their own minimum payoff.
To speed this up, **Alpha-Beta Pruning** is integrated. It maintains two values: $\alpha$ (the minimum score the maximizing player is assured of) and $\beta$ (the maximum score the minimizing player is assured of). If a branch's evaluation proves worse than a previously examined branch, the algorithm "prunes" the rest of that branch and stops exploring it, drastically reducing the number of evaluated nodes without affecting the final result.

### 2. State Representation via Bitboards
Traditional arrays are slow. This engine uses **Bitboards**—representing the entire board state as a single 64-bit integer (`uint64_t`). Using 2 bits per cell (00 = Empty, 01 = X, 10 = O), a 5x5 board occupies exactly 50 bits of a 64-bit integer.
*   **Why?** Checking win conditions becomes a matter of applying bitwise `AND`, `OR`, and bit-shifts (`<<`, `>>`). These operations map directly to single CPU cycles on modern ALUs (Arithmetic Logic Units).

### 3. Move Ordering: The Center-First Heuristic
Alpha-Beta pruning relies heavily on the *order* in which moves are evaluated. If the engine looks at the best moves first, it can prune subsequent branches much faster. 
*   **Implementation:** The engine sorts available moves based on their Euclidian distance to the center of the board. Because central squares have higher connectivity (participating in more rows, columns, and diagonals), they are statistically far more advantageous.

### 4. Dihedral Symmetry ($D_8$) Game Tree Reduction
Many board states are mathematically identical, just rotated or flipped. A square board exhibits $D_8$ symmetry (4 rotations $\times$ 2 reflections = 8 distinct orientations). 
*   **Implementation:** Before evaluating or caching a state, the engine translates it into a "canonical state" (the lowest numerical value among its 8 reflections/rotations). This collapses the size of the total state space by nearly a factor of 8.

### 5. Transposition Tables (TT)
Even with symmetries, different move orders can lead to the identical resulting board state (Transpositions).
*   **Implementation:** The engine uniquely caches previously evaluated states globally using a massive Fixed-Size Lockless Array instead of standard Hash Maps. If it encounters a state seen before by any active thread, it simply retrieves the exact evaluation in $O(1)$ time utilizing `std::atomic` variables for data-race safety without mutex contention.

---

## ⚡ Performance Optimization for Apple M2 Studio (32GB RAM)

This engine makes deliberate use of the unique architecture of the M2 Studio. 

### Thread-Level Parallelism via `std::async`
Instead of exploring the top-level branches sequentially, `src/Solver.hpp` dispatches each initial available move into its own thread using C++ `std::async(std::launch::async, ...)`. 
*   **M2 Studio Synergy:** The M2 Max/Ultra chips possess up to 12 or 24 CPU cores. Dispatching the root game tree nodes to concurrent threads completely saturates the available high-performance CPU cores, computing massive sub-trees entirely independently.

### Complete RAM Elimination (Global Lockless Caching)
To combat the inevitable SSD Memory-Swapping Death commonly associated with huge independent Hash Maps expanding infinitely across threads, this C++ engine adopts a professional chess-engine architecture.
*   **16GB Lockless Table:** It immediately statically allocates a 1,000,000,000-entry Transposition Table array. At exactly 16 GB of memory, it never consumes another byte. Every thread globally targets it using `key % 1_000_000_000` via `std::memory_order_relaxed` atomics. The engine effortlessly scales to 12+ cores sharing their computed symmetric states simultaneously without ever triggering a single thread lock or SSD memory dump.

### Unified Memory Architecture
Apple Silicon features a unified memory layout that provides the CPU direct, high-bandwidth (400+ GB/s) access to system RAM. Because our recursive functions are heavily memory-bound (constantly retrieving `uint64_t` keys from the Transposition Table), the M2's high-bandwidth capabilities perfectly supplement the engine.

---

## 🚀 Getting Started

### Prerequisites
*   A C++17 (or newer) compatible compiler (e.g., Apple Clang, GCC)
*   Make / CMake (Depending on build configuration)

### Compiling and Running
To compile natively on macOS:
```bash
clang++ -std=c++17 -O3 src/main.cpp -o tictactoe_cpp
./tictactoe_cpp
```

### Usage
When executed, you will be prompted for:
1.  **Board Size:** The dimensions of the grid (e.g., 3 for 3x3, 4 for 4x4, 5 for 5x5).
2.  **Target Win Condition:** The number of pieces required in a row to win (e.g., 3 for standard Tic-Tac-Toe).

The engine will then display the board state, execute its multi-threaded Minimax permutations, and declare its optimal outcome predictions for both player X and O along with the search duration down to the millisecond.
