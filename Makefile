# Swish build front-end. `make` (no args) prints this help.
#
#   build/        release-style tree   (SWISH_DEBUG_UI=OFF, Release)
#   build-debug/  debug-UI tree        (SWISH_DEBUG_UI=ON,  RelWithDebInfo)
# Separate trees mean switching between `make run` and `make debug` never
# reconfigures or rebuilds the other one.

BUILD_DIR       := build
DEBUG_BUILD_DIR := build-debug

# Blender binary - override if blender is not on PATH
# e.g.  make car-analyze CAR=assets/cars/foo.glb BLENDER=/Applications/Blender.app/Contents/MacOS/Blender
# Default: `blender` on PATH, else the macOS app bundle if present.
BLENDER ?= $(or $(shell command -v blender 2>/dev/null),$(wildcard /Applications/Blender.app/Contents/MacOS/Blender),blender)

# Extra args for scripts/check_repo.sh, e.g.  make check CHECK_ARGS=--strict
CHECK_ARGS ?=

.DEFAULT_GOAL := help

.PHONY: help rules feature-rules build run swish debug debug-build clean clean-debug clean-all format format-check glslc-test \
        car-analyze test test-release test-debug prune prune-check check new-class install-hooks

help: ## Show this help
	@echo "Swish make targets:"
	@grep -hE '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN{FS=":.*## "}{printf "  %-14s %s\n", $$1, $$2}'

build: ## Configure + build release (build/, debug UI OFF)
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DSWISH_DEBUG_UI=OFF
	@cmake --build $(BUILD_DIR)

run: build ## Build release, then run build/swish
	@./$(BUILD_DIR)/swish

# Live debug/tuning UI (Dear ImGui). Backtick (`) toggles edit vs drive mode.
# RelWithDebInfo = -O2 + symbols: the live-tuning UI needs a real framerate - an
# unoptimized -O0 build ran ~4x slower (36 vs ~140 fps at 1.5x SSAA) - while
# keeping debug symbols for the occasional lldb session.
# NOTE: RelWithDebInfo defines NDEBUG (CMake default `-O2 -g -DNDEBUG`; CMakeLists.txt does
# not override it) and src/renderer/VulkanContext/VulkanContext.cpp sets
# kEnableValidationLayers = false under NDEBUG. So `make run` AND `make debug` both run with
# the Khronos validation layer OFF. To get validation, configure a plain Debug build:
#   cmake -S . -B build-validation -DCMAKE_BUILD_TYPE=Debug -DSWISH_DEBUG_UI=ON && cmake --build build-validation
debug-build: ## Configure + build the debug-UI tree (build-debug/)
	@cmake -S . -B $(DEBUG_BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSWISH_DEBUG_UI=ON
	@cmake --build $(DEBUG_BUILD_DIR)

debug: debug-build ## Build debug-UI tree, then run build-debug/swish
	@./$(DEBUG_BUILD_DIR)/swish

swish: run

test: build ## Build release, run ctest (build/)
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

test-release: test

test-debug: debug-build ## Build debug-UI tree, run ctest (build-debug/)
	@ctest --test-dir $(DEBUG_BUILD_DIR) --output-on-failure

clean: ## Remove build/
	@rm -rf $(BUILD_DIR)

clean-debug: ## Remove build-debug/
	@rm -rf $(DEBUG_BUILD_DIR)

clean-all: clean clean-debug ## Remove build/ and build-debug/

rules: ## Print the coding ruleset (docs/CODING_RULES.md): read before writing code
	@cat docs/CODING_RULES.md

feature-rules: ## Print the new-feature ruleset (docs/FEATURE_RULES.md): read before planning a feature
	@cat docs/FEATURE_RULES.md

format: ## clang-format first-party code IN PLACE (scripts/format.sh)
	@bash scripts/format.sh

format-check: ## clang-format dry run, modifies nothing (exit 1 if any file differs)
	@bash scripts/format.sh --check

check: ## Repo hygiene: changelog index, stray root files, sizes, format, source globs (CHECK_ARGS=--strict)
	@bash scripts/check_repo.sh $(CHECK_ARGS)

# Scaffold src/<DIR>/<NAME>/<NAME>.{h,cpp}. Sources are globbed, so no CMake edit is needed.
#   make new-class NAME=Foo DIR=scene      (DIR may also be written src/scene)
new-class: ## Scaffold a class: make new-class NAME=Foo DIR=scene  -> src/scene/Foo/Foo.{h,cpp}
	@[ -n "$(NAME)" ] && [ -n "$(DIR)" ] || (echo "Usage: make new-class NAME=Foo DIR=scene   (creates src/scene/Foo/Foo.{h,cpp})"; exit 1)
	@bash scripts/create_object.sh "$(DIR)" "$(NAME)"

# Copies scripts/hooks/pre-commit into this checkout's hooks dir. NOT run automatically.
# Refuses to overwrite an existing hook unless FORCE=1.
install-hooks: ## Copy scripts/hooks/pre-commit into .git/hooks (opt-in; FORCE=1 to overwrite)
	@hooks="$$(git rev-parse --git-path hooks)"; \
	if [ -e "$$hooks/pre-commit" ] && [ "$(FORCE)" != "1" ]; then \
		echo "$$hooks/pre-commit already exists; re-run with FORCE=1 to overwrite"; exit 1; fi; \
	mkdir -p "$$hooks" && cp scripts/hooks/pre-commit "$$hooks/pre-commit" && chmod +x "$$hooks/pre-commit" && \
	echo "installed $$hooks/pre-commit"

glslc-test: ## Run scripts/glslc_test.sh
	@bash scripts/glslc_test.sh

# Sources are globbed by CMake (CONFIGURE_DEPENDS), so there is nothing to prune.
# Targets kept so old muscle memory / scripts keep working.
prune prune-check:
	@echo "sources are globbed by CMake (CONFIGURE_DEPENDS); nothing to prune."

# Analyze a car GLB - print stats, hierarchy tree, and part groups.
#
# Usage:
#   make car-analyze CAR=assets/cars/1999_honda_civic_si.glb
#   make car-analyze CAR=assets/cars/foo.glb FLAGS="--shorten --group --materials"
#   make car-analyze CAR=assets/cars/foo.glb FLAGS="--stats"
#
# Override BLENDER if it is not on your PATH:
#   make car-analyze CAR=... BLENDER=/Applications/Blender.app/Contents/MacOS/Blender
car-analyze: ## Analyze a car GLB (CAR=path/to.glb [FLAGS=...])
	@[ -n "$(CAR)" ] || (echo "Usage: make car-analyze CAR=<path/to/car.glb> [FLAGS='...']"; exit 1)
	@$(BLENDER) --background --python tools/car_analyzer/run.py -- $(CAR) $(FLAGS)
