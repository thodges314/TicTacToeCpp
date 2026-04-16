CXX      = clang++
CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -march=native -mtune=native

EMSDK    ?= $(HOME)/emsdk
EMCC     = $(EMSDK)/upstream/emscripten/emcc
EMFLAGS  = -O3 -std=c++20 -DWASM_BUILD \
           -s EXPORTED_FUNCTIONS='["_wasm_init","_wasm_getBestMove","_wasm_checkWin","_wasm_isDraw","_malloc","_free"]' \
           -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAP32"]' \
           -s ALLOW_MEMORY_GROWTH=1 \
           -s INITIAL_MEMORY=268435456 \
           -s MODULARIZE=1 \
           -s EXPORT_NAME='createEngineModule'

.PHONY: all book wasm serve clean

all: book wasm

# ---- Native: build and run the opening book generator ----------------------
book: tools/gen_book
	@echo "Running opening book generator..."
	@mkdir -p public
	./tools/gen_book > public/opening_book.json 2>&1 | grep -v "^  ->" || true
	./tools/gen_book 2>/dev/null > public/opening_book.json
	@echo "Written: public/opening_book.json"

tools/gen_book: tools/gen_book.cpp engine/Bitboard.hpp engine/Solver.hpp
	$(CXX) $(CXXFLAGS) $< -o $@

# ---- WASM: compile the engine with Emscripten -------------------------------
wasm: public/engine.js

public/engine.js: engine/wasm_api.cpp engine/Bitboard.hpp engine/Solver.hpp
	@mkdir -p public
	. $(EMSDK)/emsdk_env.sh 2>/dev/null && \
	$(EMCC) $(EMFLAGS) -I. $< -o $@
	@echo "Built: public/engine.js + public/engine.wasm"

# ---- Local dev server -------------------------------------------------------
# Serves the project root so /public/engine.wasm and /web/index.html work.
# Open: http://localhost:8080/web/
serve:
	@echo "Serving at http://localhost:8080/web/"
	python3 -m http.server 8080

# ---- Cleanup ----------------------------------------------------------------
clean:
	rm -f tools/gen_book public/engine.js public/engine.wasm public/opening_book.json
