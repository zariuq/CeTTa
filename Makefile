SHELL = /bin/bash
CC = gcc
CFLAGS = -O3 -Wall -Werror -std=c11 -Isrc
LDFLAGS = -lm

SRC = src/atom.c src/parser.c src/subst_tree.c src/space.c src/space_match_backend.c src/match.c src/eval.c src/grounded.c src/lang.c src/compile.c src/runtime.c src/main.c
OBJ = $(SRC:.c=.o)
BIN = cetta
SPACE_MATCH_BACKENDS = native-subst-tree native-candidate-exact pathmap-imported

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

# Fast test: compare CeTTa output against pre-computed .expected files.
# No oracle invocation — safe and instant.
test: $(BIN)
	@pass=0; fail=0; skip=0; \
	for f in tests/test_*.metta tests/he_*.metta; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
		result=$$(./$(BIN) --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat $$exp)" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	echo "$$pass passed, $$fail failed, $$skip skipped"

test-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		echo "== backend: $$backend =="; \
		pass=0; fail=0; skip=0; \
		for f in tests/test_*.metta tests/he_*.metta; do \
			[ -f "$$f" ] || continue; \
			exp="$${f%.metta}.expected"; \
			if [ ! -f "$$exp" ]; then \
				echo "SKIP: $$f (no .expected file)"; \
				skip=$$((skip + 1)); \
				continue; \
			fi; \
			result=$$(./$(BIN) --space-match-backend "$$backend" --lang he "$$f" 2>&1); \
			if [ "$$result" = "$$(cat $$exp)" ]; then \
				echo "PASS: $$f"; \
				pass=$$((pass + 1)); \
			else \
				echo "FAIL: $$f"; \
				diff <(cat "$$exp") <(echo "$$result") | head -10; \
				fail=$$((fail + 1)); \
			fi; \
		done; \
		echo "---"; \
		echo "$$pass passed, $$fail failed, $$skip skipped"; \
		[ $$fail -eq 0 ] || exit 1; \
	done

# Slow: regenerate .expected files from HE CLI oracle.
# Run ONE AT A TIME to avoid OOM. Requires conda hyperon env.
oracle-refresh:
	@for f in tests/test_*.metta tests/he_*.metta; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.metta}.expected"; \
		echo "oracle: $$f"; \
		ulimit -v 6291456; \
		source $$HOME/miniconda3/bin/activate hyperon && \
		timeout 30 metta "$$f" > "$$exp" 2>&1; \
	done; \
	echo "done — .expected files updated"

# Benchmark: forward chaining depth 3. Uses --count-only to avoid giant stdout.
# Checks theorem count matches frozen regression number.
bench-d3: $(BIN)
	@count=$$(./$(BIN) --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
	echo "depth-3 total: $$count theorems"; \
	if [ "$$count" = "3421" ]; then \
		echo "PASS: theorem count matches"; \
	else \
		echo "FAIL: expected 3421, got $$count"; exit 1; \
	fi

bench-d3-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 total: $$count theorems"; \
		if [ "$$count" = "3421" ]; then \
			echo "PASS: $$backend theorem count matches"; \
		else \
			echo "FAIL: expected 3421, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-d3-nodup: $(BIN)
	@count=$$(./$(BIN) --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
	echo "depth-3 nodup total: $$count theorems"; \
	if [ "$$count" = "3268" ]; then \
		echo "PASS: nodup theorem count matches"; \
	else \
		echo "FAIL: expected 3268, got $$count"; exit 1; \
	fi

bench-d3-nodup-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 nodup total: $$count theorems"; \
		if [ "$$count" = "3268" ]; then \
			echo "PASS: $$backend nodup theorem count matches"; \
		else \
			echo "FAIL: expected 3268, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-d4: $(BIN)
	@out=$$(ulimit -v 6291456; timeout 600 ./$(BIN) --count-only tests/nil_pc_fc_d4.metta 2>&1); \
	status=$$?; \
	count=$$(printf '%s\n' "$$out" | tail -1); \
	echo "depth-4 total: $$count theorems"; \
	if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: depth-4 produced a count under the memory cap"; \
	else \
		printf '%s\n' "$$out" | tail -5; \
		echo "FAIL: depth-4 did not produce a valid count"; exit 1; \
	fi

bench-d4-nodup: $(BIN)
	@out=$$(ulimit -v 6291456; timeout 600 ./$(BIN) --count-only tests/nil_pc_fc_d4_nodup.metta 2>&1); \
	status=$$?; \
	count=$$(printf '%s\n' "$$out" | tail -1); \
	echo "depth-4 nodup total: $$count theorems"; \
	if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: depth-4 nodup produced a count under the memory cap"; \
	else \
		printf '%s\n' "$$out" | tail -5; \
		echo "FAIL: depth-4 nodup did not produce a valid count"; exit 1; \
	fi

# LLVM IR validation: verify emitted IR compiles through opt/llc.
compile-test: $(BIN)
	@pass=0; fail=0; \
	for f in tests/test_equations.metta tests/test_basic_eval.metta tests/test_disc_trie.metta; do \
		[ -f "$$f" ] || continue; \
		ir=$$(./$(BIN) --compile "$$f" 2>&1); \
		if echo "$$ir" | opt -S -o /dev/null 2>/dev/null; then \
			echo "IR-OK: $$f"; pass=$$((pass + 1)); \
		else \
			echo "IR-FAIL: $$f"; fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; echo "$$pass passed, $$fail failed"

.PHONY: all clean test test-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends bench-d4 bench-d4-nodup compile-test
