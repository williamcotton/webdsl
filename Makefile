ifneq (,$(wildcard ./.env))
	include .env
	export
endif

PLATFORM := $(shell sh -c 'uname -s 2>/dev/null | tr 'a-z' 'A-Z'')

CC = clang
SRC_DIR = src

# Base CFLAGS from compile_flags.txt
BASE_CFLAGS = $(shell cat compile_flags.txt | tr '\n' ' ')

# Platform-specific settings
ifeq ($(PLATFORM),LINUX)
	LUA_LIB = -llua5.4
	LUA_INCLUDE = -I/usr/include/lua5.4
	PG_LIBDIR = /usr/lib/x86_64-linux-gnu
	PG_INCLUDE = -I/usr/include/postgresql
	SANITIZE_FLAGS =
	PLATFORM_LIBS = -lm -lpthread -ldl
	PROFDATA = llvm-profdata
	COV = llvm-cov
	TIDY = clang-tidy
	FORMAT = clang-format
else ifeq ($(PLATFORM),DARWIN)
	LUA_LIB = -llua
	LUA_INCLUDE = -I/opt/homebrew/include/lua
	PG_LIBDIR = /opt/homebrew/lib/postgresql@14
	PG_INCLUDE = -I/opt/homebrew/include/postgresql@14
	SANITIZE_FLAGS = -fsanitize=address,undefined,implicit-conversion,float-divide-by-zero,local-bounds,nullability,integer,function
	PLATFORM_LIBS =
	PROFDATA = $(shell brew --prefix llvm)/bin/llvm-profdata
	COV = $(shell brew --prefix llvm)/bin/llvm-cov
	TIDY = $(shell brew --prefix llvm)/bin/clang-tidy
	FORMAT = $(shell brew --prefix llvm)/bin/clang-format
endif

# Common library flags
LIBS = -lmicrohttpd -L$(PG_LIBDIR) -lpq -ljansson -ljq $(LUA_LIB) -lcurl $(PLATFORM_LIBS)

# Combine all CFLAGS
CFLAGS = $(BASE_CFLAGS) $(PG_INCLUDE) $(LUA_INCLUDE) -DBUILD_ENV=$(BUILD_ENV)
DEV_CFLAGS = -g -O0 $(SANITIZE_FLAGS)

PROJECT_SRC = $(wildcard src/*/*.c) $(wildcard src/*.c)
MAIN_SRC = src/main.c
LIB_SRC = $(filter-out $(MAIN_SRC),$(PROJECT_SRC))
SRC = $(LIB_SRC) $(wildcard deps/*/*.c)
TEST_SRC = $(wildcard test/*.c) $(wildcard test/*/*.c)
BUILD_DIR = build

ifeq ($(BUILD_ENV),development)
	TEST_CFLAGS += -DDEV_ENV
endif

.env:
	cp default.env .env

.PHONY: start
start: $(BUILD_DIR)/webdsl
	$(BUILD_DIR)/webdsl

$(BUILD_DIR)/webdsl:
	mkdir -p $(BUILD_DIR)
	$(CC) -o $(BUILD_DIR)/webdsl $(MAIN_SRC) $(SRC) $(CFLAGS) $(DEV_CFLAGS) -DERR_STACKTRACE $(LIBS)

.PHONY: test
test:
	mkdir -p $(BUILD_DIR)
	$(CC) -o $(BUILD_DIR)/$@ $(TEST_SRC) $(LIB_SRC) $(CFLAGS) $(TEST_CFLAGS) $(DEV_CFLAGS) $(LIBS)
	$(BUILD_DIR)/$@ app.webdsl

test-coverage-output:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/coverage
	$(CC) -o $(BUILD_DIR)/$@ $(TEST_SRC) $(SRC) $(CFLAGS) $(PG_CFLAGS) $(TEST_CFLAGS) $(DEV_CFLAGS) -fprofile-instr-generate -fcoverage-mapping $(LIBS)
	LLVM_PROFILE_FILE="build/test.profraw" $(BUILD_DIR)/$@
	$(PROFDATA) merge -sparse build/test.profraw -o build/test.profdata
	$(COV) show $(BUILD_DIR)/$@ -instr-profile=$(BUILD_DIR)/test.profdata -ignore-filename-regex="/deps|demo|test/"

test-coverage-html:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/coverage
	$(CC) -o $(BUILD_DIR)/$@ $(TEST_SRC) $(SRC) $(CFLAGS) $(PG_CFLAGS) $(TEST_CFLAGS) $(DEV_CFLAGS) -fprofile-instr-generate -fcoverage-mapping $(LIBS)
	LLVM_PROFILE_FILE="build/test.profraw" $(BUILD_DIR)/$@
	$(PROFDATA) merge -sparse build/test.profraw -o build/test.profdata
	$(COV) show $(BUILD_DIR)/$@ -instr-profile=$(BUILD_DIR)/test.profdata -ignore-filename-regex="/deps|demo|test/" -format=html > $(BUILD_DIR)/code-coverage.html

test-coverage:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/coverage
	$(CC) -o $(BUILD_DIR)/$@ $(TEST_SRC) $(SRC) $(CFLAGS) $(PG_CFLAGS) $(TEST_CFLAGS) $(DEV_CFLAGS) -fprofile-instr-generate -fcoverage-mapping $(LIBS)
	LLVM_PROFILE_FILE="build/test.profraw" $(BUILD_DIR)/$@
	$(PROFDATA) merge -sparse build/test.profraw -o build/test.profdata
	$(COV) report $(BUILD_DIR)/$@ -instr-profile=$(BUILD_DIR)/test.profdata -ignore-filename-regex="/deps|demo|test/"

lint:
ifeq ($(PLATFORM),LINUX)
	$(TIDY) --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-diagnostic-unused-command-line-argument -warnings-as-errors=* src/main.c -- $(CFLAGS) $(DEV_CFLAGS)
else ifeq ($(PLATFORM),DARWIN)
	$(TIDY) --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-diagnostic-unused-command-line-argument -warnings-as-errors=* src/main.c -- $(CFLAGS) $(DEV_CFLAGS)
endif

format:
	$(FORMAT) --dry-run --Werror $(SRC)

clean:
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

test-watch:
	make --no-print-directory test || :
	fswatch --event Updated -o test/*.c test/*.h src/ | xargs -n1 -I{} make --no-print-directory test

build-test-trace:
	mkdir -p $(BUILD_DIR)
ifeq ($(PLATFORM),LINUX)
	$(CC) -o $(BUILD_DIR)/test $(TEST_SRC) $(SRC) $(TEST_CFLAGS) $(CFLAGS) $(PG_CFLAGS) -g -O0 -gdwarf-4 -DUSE_VALGRIND $(LIBS)
else
	$(CC) -o $(BUILD_DIR)/test $(TEST_SRC) $(SRC) $(TEST_CFLAGS) $(CFLAGS) $(PG_CFLAGS) -g -O0 -gdwarf-4 $(LIBS)
	codesign -s - -v -f --entitlements debug.plist $(BUILD_DIR)/test
endif

build-trace:
	mkdir -p $(BUILD_DIR)
	$(CC) -o $(BUILD_DIR)/webdsl $(MAIN_SRC) $(SRC) $(CFLAGS) $(PG_CFLAGS) $(DEV_CFLAGS) -DERR_STACKTRACE $(LIBS)
ifeq ($(PLATFORM),DARWIN)
	codesign -s - -v -f --entitlements debug.plist $(BUILD_DIR)/webdsl
endif

test-leaks: build-test-trace
ifeq ($(PLATFORM),LINUX)
	valgrind --tool=memcheck --leak-check=full --suppressions=main.supp --gen-suppressions=all --error-exitcode=1 --num-callers=30 -s $(BUILD_DIR)/test
else ifeq ($(PLATFORM),DARWIN)
	leaks --atExit -- $(BUILD_DIR)/test
endif

test-analyze:
	clang --analyze $(SRC) $(CFLAGS) $(DEV_CFLAGS) -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-checker=core,deadcode,nullability,optin,osx,security,unix,valist -Xanalyzer -analyzer-disable-checker -Xanalyzer security.insecureAPI.DeprecatedOrUnsafeBufferHandling -Werror

test-threads:
	mkdir -p $(BUILD_DIR)
	$(CC) -o $(BUILD_DIR)/$@ $(TEST_SRC) $(SRC) $(CFLAGS) $(TEST_CFLAGS) -fsanitize=thread
	$(BUILD_DIR)/$@

manual-test-trace: build-test-trace
	SLEEP_TIME=5 RUN_X_TIMES=10 $(BUILD_DIR)/test

# Test-related variables
TEST_DIR = test
TEST_BUILD_DIR = build/test
TEST_SRCS = test/test_main.c test/test_lexer.c test/test_parser.c
TEST_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_BUILD_DIR)/%.o)
TEST_BINS = $(TEST_BUILD_DIR)/test_runner

# Add unity source to test objects
TEST_OBJS += $(TEST_BUILD_DIR)/unity.o

# Create test build directory - updated to create all necessary subdirectories
$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)
	mkdir -p build/src

# Compile unity
$(TEST_BUILD_DIR)/unity.o: $(TEST_DIR)/unity/unity.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile test files
$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# Link test executables - updated to include source files
$(TEST_BUILD_DIR)/test_runner: $(TEST_OBJS) $(filter-out $(BUILD_DIR)/main.o,$(wildcard $(SRC_DIR)/*.c))
	$(CC) $^ $(CFLAGS) -o $@

# Test targets
.PHONY: unit-test clean-test

unit-test: $(TEST_BUILD_DIR) $(TEST_BINS)
	./$(TEST_BUILD_DIR)/test_runner

clean-test:
	rm -rf $(TEST_BUILD_DIR)

# Update the main clean target if not already done
clean: clean-test

# Update the source files list to include main.c
SRC_FILES = src/arena.c src/lexer.c src/parser.c src/server.c src/stringbuilder.c src/main.c

# If you have a variable for object files, it should look like:
OBJ_FILES = $(SRC_FILES:.c=.o)

SERVER_SRCS = src/server/server.c src/server/handler.c src/server/api.c \
              src/server/routing.c src/server/html.c

SRCS = src/main.c src/arena.c src/db.c src/lexer.c src/parser.c \
       $(SERVER_SRCS) src/stringbuilder.c

TEST_SRCS = test/test_arena.c test/test_lexer.c test/test_main.c \
             test/test_parser.c test/test_server.c test/test_stringbuilder.c \
             test/unity/unity.c
