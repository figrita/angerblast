# Angerblast - vertical schmup roguelite (raylib).
#
# Modular build: every .c under src/ is compiled to an object and linked. Headers are
# included relative to src/ (-Isrc). Desktop build uses pkg-config to find raylib;
# the web target uses emscripten.

CC      ?= gcc
BIN      = angerblast
SRCDIR   = src
BUILDDIR = build

SRCS := $(shell find $(SRCDIR) -name '*.c')
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

CFLAGS   = -std=c99 -O2 -Wall -Wextra -I$(SRCDIR) -MMD -MP
RAYFLAGS = $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIBS  = $(shell pkg-config --libs raylib 2>/dev/null)
LDLIBS   = $(RAYLIBS) -lm -lpthread -ldl

# Fallback if pkg-config can't find raylib
ifeq ($(strip $(RAYLIBS)),)
RAYLIBS  = -lraylib
LDLIBS   = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

.PHONY: all run clean web test geoditor bosseditor

all: $(BIN)

# logic tests: link all game objects except game.o (which has main) + the test's main
TEST_OBJS := $(filter-out $(BUILDDIR)/game.o,$(OBJS))
test: $(TEST_OBJS)
	$(CC) $(CFLAGS) $(RAYFLAGS) -o $(BUILDDIR)/test_runes tests/test_runes.c $(TEST_OBJS) $(LDLIBS)
	@$(BUILDDIR)/test_runes

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(RAYFLAGS) -o $@ $(OBJS) $(LDLIBS)

# compile each source into build/, mirroring the src/ tree
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RAYFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN)

# ---- geometry + boss editor (standalone tool, links game objects minus game.o) ----
# One binary with a mode switch (SHAPES / BOSS); `bosseditor` is an alias.
GEODITOR_OBJS := $(filter-out $(BUILDDIR)/game.o,$(OBJS))
geoditor: $(GEODITOR_OBJS)
	$(CC) $(CFLAGS) $(RAYFLAGS) -o build/geoditor tools/geoditor.c $(GEODITOR_OBJS) $(LDLIBS)
bosseditor: geoditor

clean:
	rm -rf $(BUILDDIR) $(BIN) angerblast.html angerblast.js angerblast.wasm angerblast.data \
		web/angerblast.html web/angerblast.js web/angerblast.wasm web/angerblast.data

# --- Web (WebAssembly) build ---
# Requires emscripten (emcc on PATH, e.g. `source ~/emsdk/emsdk_env.sh`) and a raylib built
# for web. Build raylib web once:  (cd ~/raylib/src && emmake make PLATFORM=PLATFORM_WEB)
# Then:  make web              (defaults to ~/raylib/src for the lib)
#   or:  make web RAYLIB_WEB=/path/to/raylib/src
RAYLIB_WEB ?= $(HOME)/raylib/src
# search.c uses fork()/wait() (offline batch + Monte-Carlo) — exclude it from the web build;
# game.c #ifdef-guards out the offline modes that call into it.
WEB_SRCS := $(filter-out $(SRCDIR)/systems/search.c,$(SRCS))
web:
	emcc -o web/angerblast.html $(WEB_SRCS) -Os -Wall -I$(SRCDIR) \
		$(RAYLIB_WEB)/libraylib.web.a -I$(RAYLIB_WEB) \
		-s USE_GLFW=3 -s ASYNCIFY -s TOTAL_MEMORY=67108864 -s FORCE_FILESYSTEM=1 \
		-DPLATFORM_WEB --preload-file assets \
		--shell-file web/shell.html
	@echo "WASM built -> web/angerblast.html  (serve with: python3 -m http.server -d web)"

-include $(DEPS)

# fork-search a fleet of seeds in parallel (uses every core): make search-fleet SEEDS=24
SEEDS ?= 24
search-fleet: $(BIN)
	@for s in $$(seq 1 $(SEEDS)); do ./$(BIN) --search --seed $$s --depth 5 --frames 36000 >/dev/null 2>&1 & done; wait
	@python3 tools/fork_aggregate.py
