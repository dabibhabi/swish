BUILD_DIR := build
EXECUTABLE := $(BUILD_DIR)/swish

# Blender binary — override if blender is not on PATH
# e.g.  make car-analyze CAR=cars/foo.glb BLENDER=/Applications/Blender.app/Contents/MacOS/Blender
BLENDER ?= blender

.PHONY: build run swish debug clean format glslc-test car-analyze test

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DSWISH_DEBUG_UI=OFF
	@cmake --build $(BUILD_DIR)

run:
	@./$(EXECUTABLE)

# In-engine live debug/tuning UI (Dear ImGui). Configures with the debug-UI
# option ON, builds, and runs. Backtick (`) toggles edit vs drive mode.
debug:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DSWISH_DEBUG_UI=ON
	@cmake --build $(BUILD_DIR)
	@./$(EXECUTABLE)

swish: build run

test: build
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

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
