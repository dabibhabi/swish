BUILD_DIR := build
EXECUTABLE := $(BUILD_DIR)/swish

# Blender binary — override if blender is not on PATH
# e.g.  make car-analyze CAR=cars/foo.glb BLENDER=/Applications/Blender.app/Contents/MacOS/Blender
BLENDER ?= blender

.PHONY: build run swish debug clean format glslc-test car-analyze test prune prune-check

build: prune-check
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DSWISH_DEBUG_UI=OFF
	@cmake --build $(BUILD_DIR)

run:
	@./$(EXECUTABLE)

# In-engine live debug/tuning UI (Dear ImGui). Configures with the debug-UI
# option ON, builds, and runs. Backtick (`) toggles edit vs drive mode.
# RelWithDebInfo = -O2 + symbols: the live-tuning UI needs a real framerate — an
# unoptimized -O0 build ran ~4× slower (36 vs ~140 fps at 1.5× SSAA) — while
# keeping debug symbols for the occasional lldb session. Validation still runs.
debug:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSWISH_DEBUG_UI=ON
	@cmake --build $(BUILD_DIR)
	@./$(EXECUTABLE)

swish: build run

test: build
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

# Remove dead sources (deleted modules) from the swish target in CMakeLists.txt.
prune:
	@bash scripts/prune_sources.sh

# Report dead sources without editing CMakeLists.txt. Run before `build` as a
# non-fatal warning so a deleted module is flagged (not silently rewritten).
prune-check:
	@bash scripts/prune_sources.sh --check || \
		echo "  -> run 'make prune' to remove them, then rebuild."

format:
	@bash scripts/format.sh

glslc-test:
	@bash scripts/glslc_test.sh

# Analyze a car GLB — print stats, hierarchy tree, and part groups.
#
# Usage:
#   make car-analyze CAR=cars/1999_honda_civic_si.glb
#   make car-analyze CAR=cars/foo.glb FLAGS="--shorten --group --materials"
#   make car-analyze CAR=cars/foo.glb FLAGS="--stats"
#
# Override BLENDER if it is not on your PATH:
#   make car-analyze CAR=... BLENDER=/Applications/Blender.app/Contents/MacOS/Blender
car-analyze:
	@[ -n "$(CAR)" ] || (echo "Usage: make car-analyze CAR=<path/to/car.glb> [FLAGS='...']"; exit 1)
	@$(BLENDER) --background --python tools/car_analyzer/run.py -- $(CAR) $(FLAGS)
