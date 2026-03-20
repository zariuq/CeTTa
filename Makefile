SHELL = /bin/bash
CC = gcc
CFLAGS = -O3 -Wall -Werror -std=c11 -Isrc
LDFLAGS = -lm

SRC = src/atom.c src/parser.c src/space.c src/match.c src/eval.c src/grounded.c src/lang.c src/compile.c src/runtime.c src/main.c
OBJ = $(SRC:.c=.o)
BIN = cetta

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

.PHONY: all clean test oracle-refresh
