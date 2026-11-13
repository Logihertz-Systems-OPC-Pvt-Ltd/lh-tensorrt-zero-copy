CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Werror -pedantic -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -lm

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin
TEST_DIR = tests

CORE_SRCS = $(SRC_DIR)/dma_buf_pool.cpp \
            $(SRC_DIR)/color_tensor_bridge.cpp \
            $(SRC_DIR)/json_parser.cpp \
            $(SRC_DIR)/trt_auditor.cpp \
            $(SRC_DIR)/exporters.cpp

CORE_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(CORE_SRCS))

MAIN_SRC = $(SRC_DIR)/main.cpp
MAIN_OBJ = $(BUILD_DIR)/main.o
CLI_BIN = $(BIN_DIR)/lh-trt-zerocopy

TEST_SRC = $(TEST_DIR)/test_runner.cpp
TEST_BIN = $(TEST_DIR)/test_runner

.PHONY: all test clean

all: $(CLI_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CLI_BIN): $(CORE_OBJS) $(MAIN_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(CORE_OBJS) $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(TEST_BIN)
