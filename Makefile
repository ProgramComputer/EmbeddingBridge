DEBUG ?= 1
CC = gcc
CFLAGS = -Wall -Wextra -I./src/core -I./src/cli -I$(INCLUDEDIR) -fPIC -DEB_ENABLE_MEMORY_STORE -I/usr/local/include

# Debug-specific flags
ifeq ($(DEBUG), 1)
    CFLAGS += -g -DEB_DEBUG
else
    CFLAGS += -O2
endif

# Add curl for HTTP transport
LDFLAGS = -lm -lssl -lcrypto -lgit2 -lnpy_array -lcurl

# Installation paths - default to user-local installation
PREFIX ?= $(HOME)/.local
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include/embedding_bridge

SRC_DIR = src/core
TEST_DIR = tests/c
OBJ_DIR = obj
BIN_DIR = bin
TEST_BIN_DIR = $(BIN_DIR)/tests
LIB_DIR = lib

# Source files
CORE_SRCS = $(filter-out $(SRC_DIR)/main.c, $(wildcard $(SRC_DIR)/*.c))
CLI_SRCS = $(wildcard src/cli/*.c)
CORE_OBJS = $(CORE_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
CLI_OBJS = $(CLI_SRCS:src/cli/%.c=$(OBJ_DIR)/cli_%.o)
OBJS = $(CORE_OBJS) $(CLI_OBJS)

# Additional dependencies for transport
TRANSPORT_DEPS = $(OBJ_DIR)/transport.o $(OBJ_DIR)/transport_ssh.o $(OBJ_DIR)/transport_http.o $(OBJ_DIR)/transport_local.o

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(OBJ_DIR)/%.o)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_BIN_DIR)/%)

# Targets
TARGET = $(BIN_DIR)/embedding_bridge
LIB_TARGET = $(LIB_DIR)/libembedding_bridge.so

# Main targets
.PHONY: all clean test lib python-test valgrind memtest test-registry test-all test-c test-transport

all: $(TARGET) lib

lib: $(LIB_TARGET)

# Test targets organized by type
test-all: test-c python-test

test-c: memtest test-registry test-transport
	@echo "All C tests completed successfully"

test: test-all

test-registry: $(TEST_BIN_DIR)/test_model_registry
	@echo "Running model registry tests..."
	$(TEST_BIN_DIR)/test_model_registry

test-transport: $(TEST_BIN_DIR)/test_transport
	@echo "Running transport tests..."
	$(TEST_BIN_DIR)/test_transport

python-test: install
	@echo "Running Python CLI tests..."
	DEBUG=1 PYTHONPATH=src/python:. LD_LIBRARY_PATH=lib pytest tests/python/test_cli.py -v -s

memtest: $(TEST_BIN_DIR)/test_lib
	@echo "Running memory tests..."
	$(TEST_BIN_DIR)/test_lib

$(TEST_BIN_DIR)/test_lib: $(OBJ_DIR)/test_lib.o $(OBJ_DIR)/store.o $(OBJ_DIR)/error.o $(OBJ_DIR)/git.o $(OBJ_DIR)/embedding.o $(OBJ_DIR)/metrics.o
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_BIN_DIR)/test_transport: $(OBJ_DIR)/test_transport.o $(TRANSPORT_DEPS) $(OBJ_DIR)/error.o $(OBJ_DIR)/debug.o $(OBJ_DIR)/config.o $(OBJ_DIR)/fs.o
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

valgrind: memtest
	@echo "Running Valgrind memory leak check on library..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 $(TEST_BIN_DIR)/test_lib

$(LIB_TARGET): $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	@mkdir -p $(LIB_DIR)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(TARGET): $(OBJ_DIR)/cli_main.o $(filter-out $(OBJ_DIR)/main.o,$(CORE_OBJS)) $(filter-out $(OBJ_DIR)/cli_main.o,$(CLI_OBJS))
	@mkdir -p $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cli_%.o: src/cli/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN_DIR)/%: $(OBJ_DIR)/%.o $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_BIN_DIR)/test_model_registry: $(OBJ_DIR)/test_model_registry.o $(OBJ_DIR)/store.o $(OBJ_DIR)/error.o
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)
	find . -name "*.pyc" -delete
	find . -name "__pycache__" -delete

install: all
	@echo "Installing embedding_bridge..."
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/eb
	install -m 644 $(LIB_TARGET) $(DESTDIR)$(LIBDIR)
	install -m 644 $(SRC_DIR)/*.h $(DESTDIR)$(INCLUDEDIR)
	@echo "Installation complete. Run 'sudo ldconfig' to update library cache."

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/eb
	rm -f $(DESTDIR)$(LIBDIR)/libembedding_bridge.so
	rm -rf $(DESTDIR)$(INCLUDEDIR)
	@echo "Uninstallation complete."