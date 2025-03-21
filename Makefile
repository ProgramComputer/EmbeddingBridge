DEBUG ?= 1
CC = gcc
CFLAGS = -Wall -Wextra -I./src/core -I./src/cli -I./include -I$(INCLUDEDIR) -fPIC -DEB_ENABLE_MEMORY_STORE -I/usr/local/include $(AWS_INCLUDE) -DEB_HAVE_AWS

# Add GLib includes needed for Arrow GLib bindings
GLIB_INCLUDE = -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I/usr/include/sysprof-6

# Main build targets:
# make            - Build everything including Arrow with GLib bindings
# make build      - Build Arrow with GLib bindings only
# make build-arrow - Build Arrow C++ only (without GLib)
# make build-arrow-glib - Build Arrow with GLib bindings
# make clean      - Clean built files but preserve Arrow libraries
# make clean-all  - Clean everything including Arrow libraries

# Debug-specific flags
ifeq ($(DEBUG), 1)
    CFLAGS += -g -DEB_DEBUG -DEB_DEBUG_ENABLED
else
    CFLAGS += -O2
endif

# Check for Arrow/Parquet libraries
# Use pkg-config with appropriate paths for all calls
ARROW_INSTALL_DIR ?= $(shell pwd)/vendor/dist

# Set PKG_CONFIG_PATH only if Arrow is in the non-standard location
ifneq ($(ARROW_INSTALL_DIR),/usr/local)
  PKG_CONFIG_PATH := $(ARROW_INSTALL_DIR)/lib/pkgconfig:$(PKG_CONFIG_PATH)
endif
export PKG_CONFIG_PATH
export LD_LIBRARY_PATH

# Assume Arrow and Parquet are available since we build them
ARROW_FOUND := 1
PARQUET_FOUND := 1

# Add flags for Arrow/Parquet
ARROW_CFLAGS := -I$(ARROW_INSTALL_DIR)/include -DEB_HAVE_ARROW_PARQUET
ARROW_LIBS := -L$(ARROW_INSTALL_DIR)/lib -L$(ARROW_INSTALL_DIR)/lib/x86_64-linux-gnu -larrow -lparquet -larrow-glib -lparquet-glib -lglib-2.0 -lgobject-2.0
CFLAGS += $(ARROW_CFLAGS) $(GLIB_INCLUDE)
LDFLAGS += $(ARROW_LIBS)
$(info Arrow and Parquet found, enabling Parquet transformer)

# Add curl for HTTP transport and zstd for compression
LDFLAGS += -lm -lssl -lcrypto -lgit2 -lnpy_array -lcurl -lzstd -ljansson

# Change to more explicitly indicate and configure ZSTD library
ZSTD_CFLAGS = -DZSTD_STATIC_LINKING_ONLY
ZSTD_LIBS = -lzstd
CFLAGS += $(ZSTD_CFLAGS)
LDFLAGS += $(ZSTD_LIBS)

# AWS C SDK libraries
AWS_DIR = vendor/aws
AWS_LIBS = -laws-c-s3 -laws-c-auth -laws-c-http -laws-c-io -laws-c-cal -laws-c-common -laws-checksums -laws-c-compression -laws-c-sdkutils -ls2n -lcrypto -lssl -ldl -lpthread -lm -lrt
AWS_INCLUDE = -I$(AWS_DIR)/install/include -I$(AWS_DIR)/aws-c-s3/include -I$(AWS_DIR)/aws-c-http/include -I$(AWS_DIR)/aws-c-io/include
AWS_LIB_PATH = -L$(AWS_DIR)/install/lib

# Add AWS libraries to build
AWS_BUILT = $(AWS_DIR)/install/lib/libaws-c-s3.a $(AWS_DIR)/install/lib/libaws-c-http.a $(AWS_DIR)/install/lib/libaws-c-auth.a $(AWS_DIR)/install/lib/libaws-c-io.a $(AWS_DIR)/install/lib/libaws-c-cal.a $(AWS_DIR)/install/lib/libaws-c-common.a $(AWS_DIR)/install/lib/libaws-checksums.a $(AWS_DIR)/install/lib/libaws-c-compression.a $(AWS_DIR)/install/lib/libaws-c-sdkutils.a $(AWS_DIR)/install/lib/libs2n.a

# Add AWS to CFLAGS and LDFLAGS if available
ifneq ($(wildcard $(AWS_DIR)/aws-c-common),)
    CFLAGS += $(AWS_INCLUDE) -DEB_HAVE_AWS
    LDFLAGS += $(AWS_LIB_PATH) $(AWS_LIBS)
    $(info AWS C SDK found, enabling S3 transport)
else
    $(warning AWS C SDK not found. S3 transport functionality requires AWS libraries)
endif

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

# Additional dependencies for remote operations
REMOTE_DEPS = $(OBJ_DIR)/remote.o $(OBJ_DIR)/compress.o $(OBJ_DIR)/transformer.o $(OBJ_DIR)/json_transformer.o $(OBJ_DIR)/status.o $(OBJ_DIR)/debug.o
CLI_DEPS = $(OBJ_DIR)/cli_cli.o $(OBJ_DIR)/cli_options.o

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(OBJ_DIR)/%.o)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_BIN_DIR)/%)

# Targets
TARGET = $(BIN_DIR)/embedding_bridge
LIB_TARGET = $(LIB_DIR)/libembedding_bridge.so

# Check if Arrow is already built
ARROW_BUILT := $(shell test -f "$(ARROW_INSTALL_DIR)/lib/libarrow.so" && echo "yes" || echo "no")
ARROW_GLIB_BUILT := $(shell test -f "$(ARROW_INSTALL_DIR)/lib/libarrow-glib.so" && echo "yes" || echo "no")

# Only depend on building Arrow if it's not already built
ifeq ($(ARROW_BUILT), no)
  ARROW_DEPS := build-arrow
else
  ARROW_DEPS :=
endif

# Only depend on building Arrow GLib if it's not already built
ifeq ($(ARROW_GLIB_BUILT), no)
  ARROW_GLIB_DEPS := build-arrow-glib
else
  ARROW_GLIB_DEPS :=
endif

# Main targets
.PHONY: all clean test lib python-test valgrind memtest test-all test-c unified-test test-parquet build build-aws

# Modified targets to avoid always rebuilding Arrow
all: $(TARGET) lib

# Build just checks if Arrow is available and warns if not
build: $(ARROW_DEPS)
	@if [ "$(ARROW_BUILT)" = "no" ]; then \
		echo "Warning: Arrow libraries not found. Run 'make build-arrow' to build them."; \
	fi

lib: $(LIB_TARGET)

# Test targets organized by type
test-all: test-c python-test

test-c: memtest unified-test test-parquet
	@echo "All C tests completed successfully"

test: test-all

# Unified test rule that runs all C tests at once
unified-test: $(TEST_BINS)
	@echo "Running all C tests..."
	@for test in $(TEST_BINS); do \
		echo "\n=== Running $$test ==="; \
		$$test || exit 1; \
	done
	@echo "All tests completed successfully"

# Individual test rules still available for selective testing
test-registry: $(TEST_BIN_DIR)/test_model_registry
	@echo "Running model registry tests..."
	$(TEST_BIN_DIR)/test_model_registry

test-transport: $(TEST_BIN_DIR)/test_transport
	@echo "Running transport tests..."
	$(TEST_BIN_DIR)/test_transport

test-remote: $(OBJ_DIR)/test_remote.o
	@echo "Running remote operation tests..."
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) -o $(TEST_BIN_DIR)/test_remote $(OBJ_DIR)/test_remote.o $(OBJ_DIR)/remote.o $(OBJ_DIR)/compress.o $(OBJ_DIR)/transformer.o $(OBJ_DIR)/json_transformer.o $(OBJ_DIR)/parquet_transformer.o $(OBJ_DIR)/status.o $(OBJ_DIR)/debug.o $(OBJ_DIR)/error.o $(OBJ_DIR)/builtin_transformers.o $(LDFLAGS)
	$(TEST_BIN_DIR)/test_remote

test-dataset: $(OBJ_DIR)/test_dataset.o
	@echo "Running dataset tests..."
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) -o $(TEST_BIN_DIR)/test_dataset $(OBJ_DIR)/test_dataset.o $(OBJ_DIR)/remote.o $(OBJ_DIR)/compress.o $(OBJ_DIR)/transformer.o $(OBJ_DIR)/json_transformer.o $(OBJ_DIR)/parquet_transformer.o $(OBJ_DIR)/status.o $(OBJ_DIR)/debug.o $(OBJ_DIR)/error.o $(OBJ_DIR)/builtin_transformers.o $(LDFLAGS)
	$(TEST_BIN_DIR)/test_dataset

test-parquet: 
	@echo "Running Parquet transformer tests without Arrow libraries..."
	$(CC) -Wall -Wextra -o test_parquet test_parquet.c
	./test_parquet

test-parquet-arrows: build-arrow
	@echo "Running Parquet transformer tests with Arrow libraries..."
	@mkdir -p $(TEST_BIN_DIR)
	$(CXX) -Wall -Wextra $(ARROW_CFLAGS) -I./include -I./src/core -o $(TEST_BIN_DIR)/test_parquet_transformer \
		tests/c/test_parquet_transformer.c src/core/parquet_transformer.c src/core/transformer.c \
		$(ARROW_LIBS) -lstdc++
	$(TEST_BIN_DIR)/test_parquet_transformer

# Include both tests in the test suite
test-c: memtest unified-test test-parquet
test-c-full: test-c test-parquet-arrows

python-test: install
	@echo "Running Python CLI tests..."
	DEBUG=1 PYTHONPATH=src/python:. pytest tests/python/test_cli.py -v -s

memtest: $(TEST_BIN_DIR)/test_lib
	@echo "Running memory tests..."
	$(TEST_BIN_DIR)/test_lib

$(TEST_BIN_DIR)/test_lib: $(OBJ_DIR)/test_lib.o $(OBJ_DIR)/store.o $(OBJ_DIR)/error.o $(OBJ_DIR)/git.o $(OBJ_DIR)/embedding.o $(OBJ_DIR)/metrics.o
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_BIN_DIR)/test_transport: $(OBJ_DIR)/test_transport.o $(TRANSPORT_DEPS) $(OBJ_DIR)/error.o $(OBJ_DIR)/debug.o $(OBJ_DIR)/config.o $(OBJ_DIR)/fs.o
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_BIN_DIR)/test_model_registry: $(OBJ_DIR)/test_model_registry.o $(OBJ_DIR)/store.o $(OBJ_DIR)/error.o
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_BIN_DIR)/test_parquet_transformer: $(OBJ_DIR)/test_parquet_transformer.o src/core/parquet_transformer.c
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) -I./include -I./src/core -o $@ $^ $(LDFLAGS)

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

$(OBJ_DIR)/config.o: src/cli/config.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/embedding.o: src/core/embedding.c
$(OBJ_DIR)/format.o: src/core/format.c
$(OBJ_DIR)/search.o: src/core/search.c

$(TEST_BIN_DIR)/%: $(OBJ_DIR)/%.o $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)
	find . -name "*.pyc" -delete
	find . -name "__pycache__" -delete
	@echo "Note: Arrow libraries in vendor/dist are preserved. Use 'make clean-all' to remove them."

clean-all: clean
	rm -rf vendor/dist
	@echo "All build artifacts including Arrow libraries have been removed."

install: all
	@echo "Installing embedding_bridge..."
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/eb
	install -m 644 $(LIB_TARGET) $(DESTDIR)$(LIBDIR)
	install -m 644 $(SRC_DIR)/*.h $(DESTDIR)$(INCLUDEDIR)
	@echo "Installation complete."
	@if [ "$(DESTDIR)" = "" ] && [ "$(PREFIX)" = "/usr/local" ]; then \
		echo "Running ldconfig to update library cache..."; \
		ldconfig || (echo "Failed to run ldconfig. You may need to run 'sudo ldconfig' manually."); \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/eb
	rm -f $(DESTDIR)$(LIBDIR)/libembedding_bridge.so
	rm -rf $(DESTDIR)$(INCLUDEDIR)
	@echo "Uninstallation complete."

build-arrow:
	@if [ -f "$(ARROW_INSTALL_DIR)/lib/libarrow.so" ]; then \
		echo "Arrow C++ already built, skipping."; \
	else \
		echo "Building Arrow from source..."; \
		mkdir -p scripts; \
		chmod +x scripts/build_arrow.sh; \
		scripts/build_arrow.sh; \
	fi
	@echo "Arrow C++ 12.0.0 build complete."
	@if [ "$(ARROW_INSTALL_DIR)" != "/usr/local" ]; then \
		echo "To use this build, set environment variables:"; \
		echo "export PKG_CONFIG_PATH=\"$(ARROW_INSTALL_DIR)/lib/pkgconfig:\$$PKG_CONFIG_PATH\""; \
	else \
		echo "Libraries should be automatically found by the system."; \
	fi

build-arrow-glib: build-arrow
	@echo "Building Arrow with GLib bindings..."
	@echo "This will build both Arrow C++ and GLib bindings for C applications"
	@if [ -f "$(ARROW_INSTALL_DIR)/lib/libarrow-glib.so" ]; then \
		echo "Arrow GLib already built, skipping."; \
	else \
		scripts/build_arrow.sh --with-glib; \
	fi
	@echo "Arrow with GLib bindings built successfully."
	@echo "The PKG_CONFIG_PATH and LD_LIBRARY_PATH have been set in the Makefile."
	@echo "To use Arrow outside of make, set:"
	@echo "export PKG_CONFIG_PATH=\"$(ARROW_INSTALL_DIR)/lib/pkgconfig:\$$PKG_CONFIG_PATH\""
	@echo "export LD_LIBRARY_PATH=\"$(ARROW_INSTALL_DIR)/lib:\$$LD_LIBRARY_PATH\""

# Add a special target for building everything including Arrow 
build-all: $(TARGET) lib build-arrow build-arrow-glib

# Add a new rule for the cleanup test
test-cleanup: obj/cleanup_test.o obj/transformer.o obj/json_transformer.o obj/parquet_transformer.o obj/status.o obj/debug.o obj/error.o obj/builtin_transformers.o
	@mkdir -p bin/tests
	@echo "Running transformer cleanup test..."
	$(CC) -o bin/tests/cleanup_test $^ $(LDFLAGS)
	bin/tests/cleanup_test

obj/cleanup_test.o: tests/c/cleanup_test.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test transport layer
test-transport: obj/test_transport.o obj/transport.o obj/transport_local.o obj/status.o obj/debug.o obj/error.o
	@mkdir -p bin/tests
	@echo "Running transport tests..."
	$(CC) -o bin/tests/test_transport $^ $(LDFLAGS)
	bin/tests/test_transport

obj/test_transport.o: tests/c/test_transport.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test dataset operations
test-dataset: obj/test_dataset.o obj/remote.o obj/transformer.o obj/json_transformer.o obj/parquet_transformer.o obj/compress.o obj/status.o obj/debug.o obj/error.o obj/builtin_transformers.o
	@mkdir -p bin/tests
	@echo "Running dataset tests..."
	$(CC) -o bin/tests/test_dataset $^ $(LDFLAGS)
	bin/tests/test_dataset

obj/test_dataset.o: tests/c/test_dataset.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test S3 remote operations
test-s3-remote: obj/test_s3_remote.o obj/remote.o obj/transformer.o obj/transport.o obj/transport_ssh.o obj/transport_http.o obj/transport_local.o obj/transport_s3.o obj/json_transformer.o obj/parquet_transformer.o obj/compress.o obj/status.o obj/debug.o obj/error.o obj/builtin_transformers.o
	@mkdir -p bin/tests
	@echo "Running S3 remote tests..."
	$(CC) -o bin/tests/test_s3_remote $^ $(AWS_LIB_PATH) $(AWS_LIBS) $(LDFLAGS)
	bin/tests/test_s3_remote

obj/test_s3_remote.o: tests/c/test_s3_remote.c
	$(CC) $(CFLAGS) -c $< -o $@

obj/transport_s3.o: src/core/transport_s3.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build AWS C libraries
build-aws: build-aws-c-common build-aws-c-cal build-aws-c-io build-aws-c-http build-aws-c-auth build-aws-c-s3

build-aws-c-common:
	@echo "Building aws-c-common..."
	@mkdir -p $(AWS_DIR)/aws-c-common/build
	@cd $(AWS_DIR)/aws-c-common/build && cmake .. -DCMAKE_INSTALL_PREFIX=. -DCMAKE_PREFIX_PATH=. -DCMAKE_BUILD_TYPE=Release && make && make install
	@touch $(AWS_DIR)/aws-c-common/build/lib/libaws-c-common.a

build-aws-c-cal: build-aws-c-common
	@echo "Building aws-c-cal..."
	@mkdir -p $(AWS_DIR)/aws-c-cal/build
	@cd $(AWS_DIR)/aws-c-cal/build && cmake .. -DCMAKE_INSTALL_PREFIX=. -DCMAKE_PREFIX_PATH="$(PWD)/$(AWS_DIR)/aws-c-common/build/lib/cmake/aws-c-common" -DCMAKE_BUILD_TYPE=Release && make && make install
	@touch $(AWS_DIR)/aws-c-cal/build/lib/libaws-c-cal.a

build-aws-c-io: build-aws-c-cal
	@echo "Building aws-c-io..."
	@mkdir -p $(AWS_DIR)/aws-c-io/build
	@cd $(AWS_DIR)/aws-c-io/build && cmake .. -DCMAKE_INSTALL_PREFIX=. -DCMAKE_PREFIX_PATH="$(PWD)/$(AWS_DIR)/aws-c-common/build/lib/cmake/aws-c-common" -DCMAKE_BUILD_TYPE=Release && make && make install
	@touch $(AWS_DIR)/aws-c-io/build/lib/libaws-c-io.a

build-aws-c-http: build-aws-c-io
	@echo "Building aws-c-http..."
	@mkdir -p $(AWS_DIR)/aws-c-http/build
	@cd $(AWS_DIR)/aws-c-http/build && cmake .. -DCMAKE_INSTALL_PREFIX=. -DCMAKE_PREFIX_PATH="$(PWD)/$(AWS_DIR)/aws-c-common/build/lib/cmake/aws-c-common" -DCMAKE_BUILD_TYPE=Release && make && make install
	@touch $(AWS_DIR)/aws-c-http/build/lib/libaws-c-http.a

build-aws-c-auth: build-aws-c-http
	@echo "Building aws-c-auth..."
	@mkdir -p $(AWS_DIR)/aws-c-auth/build
	@cd $(AWS_DIR)/aws-c-auth/build && cmake .. -DCMAKE_INSTALL_PREFIX=. -DCMAKE_PREFIX_PATH="$(PWD)/$(AWS_DIR)/aws-c-common/build/lib/cmake/aws-c-common" -DCMAKE_BUILD_TYPE=Release && make && make install
	@touch $(AWS_DIR)/aws-c-auth/build/lib/libaws-c-auth.a

build-aws-c-s3: build-aws-c-auth
	@echo "Building aws-c-s3..."
	@mkdir -p $(AWS_DIR)/aws-c-s3/build
	@cd $(AWS_DIR)/aws-c-s3/build && cmake .. -DCMAKE_INSTALL_PREFIX=. -DCMAKE_PREFIX_PATH="$(PWD)/$(AWS_DIR)/aws-c-common/build/lib/cmake/aws-c-common" -DCMAKE_BUILD_TYPE=Release && make && make install
	@touch $(AWS_DIR)/aws-c-s3/build/lib/libaws-c-s3.a