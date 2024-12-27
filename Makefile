ifneq (,$(wildcard ./.env))
	include .env
	export
endif

PLATFORM := $(shell sh -c 'uname -s 2>/dev/null | tr 'a-z' 'A-Z'')

CC = clang
PROFDATA = llvm-profdata
COV = llvm-cov
TIDY = clang-tidy
FORMAT = clang-format
SRC_DIR = src

CFLAGS = $(shell cat compile_flags.txt | tr '\n' ' ')
CFLAGS += -DBUILD_ENV=$(BUILD_ENV)
PG_CFLAGS = -I$(shell pg_config --includedir)
PG_LDFLAGS = -L$(shell pg_config --libdir) -lpq
LIBS = -lmicrohttpd $(PG_LDFLAGS)
DEV_CFLAGS = -g -O0
# TEST_CFLAGS = -Werror
PROJECT_SRC = $(wildcard src/*/*.c) $(wildcard src/*.c)
MAIN_SRC = src/main.c
LIB_SRC = $(filter-out $(MAIN_SRC),$(PROJECT_SRC))
SRC = $(LIB_SRC) $(wildcard deps/*/*.c)
TEST_SRC = $(wildcard test/*.c) $(wildcard test/*/*.c)
BUILD_DIR = build

ifeq ($(BUILD_ENV),development)
	TEST_CFLAGS += -DDEV_ENV
endif

ifeq ($(PLATFORM),LINUX)
	CFLAGS += -lm -lBlocksRuntime -ldispatch -lbsd -luuid -lpthread -ldl
	TEST_CFLAGS += -Wl,--wrap=stat -Wl,--wrap=regcomp -Wl,--wrap=accept -Wl,--wrap=socket -Wl,--wrap=epoll_ctl -Wl,--wrap=listen
	PROD_CFLAGS = -Ofast
else ifeq ($(PLATFORM),DARWIN)
	DEV_CFLAGS += -fsanitize=address,undefined,implicit-conversion,float-divide-by-zero,local-bounds,nullability,integer,function
	PROD_CFLAGS = -Ofast
endif

.env:
	cp default.env .env

.PHONY: start
start: $(BUILD_DIR)/webdsl
	$(BUILD_DIR)/webdsl

$(BUILD_DIR)/webdsl:
	mkdir -p $(BUILD_DIR)
	$(CC) -o $(BUILD_DIR)/webdsl $(MAIN_SRC) $(SRC) $(CFLAGS) $(DEV_CFLAGS) -DERR_STACKTRACE

.PHONY: test
test:
	mkdir -p $(BUILD_DIR)
	$(CC) -o $(BUILD_DIR)/$@ $(TEST_SRC) $(LIB_SRC) $(CFLAGS) $(PG_CFLAGS) $(TEST_CFLAGS) $(DEV_CFLAGS) $(LIBS)
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
	$(TIDY) --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-diagnostic-unused-command-line-argument -warnings-as-errors=* src/main.c
else ifeq ($(PLATFORM),DARWIN)
	$(TIDY) --checks=-clang-diagnostic-unused-command-line-argument -warnings-as-errors=* src/main.c
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
	$(CC) -o $(BUILD_DIR)/test $(TEST_SRC) $(SRC) $(TEST_CFLAGS) $(CFLAGS) $(PG_CFLAGS) -g -O0 -gdwarf-4 $(LIBS)
ifeq ($(PLATFORM),DARWIN)
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
	clang --analyze $(SRC) $(shell cat compile_flags.txt | tr '\n' ' ') -I$(shell pg_config --includedir) -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-checker=core,deadcode,nullability,optin,osx,security,unix,valist -Xanalyzer -analyzer-disable-checker -Xanalyzer security.insecureAPI.DeprecatedOrUnsafeBufferHandling -Werror

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
