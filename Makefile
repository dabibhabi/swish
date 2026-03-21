BUILD_DIR := build
EXECUTABLE := $(BUILD_DIR)/swish

.PHONY: build run swish clean format glslc-test

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR)

run:
	@./$(EXECUTABLE)

swish: build run

clean:
	@rm -rf $(BUILD_DIR)

format:
	@bash scripts/format.sh

glslc-test:
	@bash scripts/glslc_test.sh
