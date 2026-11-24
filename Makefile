# ratslap-g305x — build and test
#
# `make`       build the CLI binary
# `make test`  unit tests + mock-hidraw integration test (no mouse needed)
CC       ?= cc
CFLAGS   ?= -O2
WARN      = -std=c99 -Wall -Wextra -Werror
DEFS      = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
ALL_CFLAGS = $(WARN) $(DEFS) $(CFLAGS)

BIN      = ratslap-g305x
LIB_SRC  = src/hidpp.c src/onboard.c src/actions.c
OBJ      = $(LIB_SRC:.c=.o) src/main.o

TEST_BINS = tests/test_crc
MOCK_SO   = tests/mock_hidraw.so

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJ)

%.o: %.c src/util.h src/hidpp.h src/onboard.h src/actions.h
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

tests/golden.h: tests/gen_golden.py
	python3 tests/gen_golden.py > $@

tests/test_%: tests/test_%.c $(LIB_SRC) src/util.h src/hidpp.h src/onboard.h src/actions.h tests/golden.h
	$(CC) $(ALL_CFLAGS) -Isrc -Itests -o $@ tests/test_$*.c $(LIB_SRC)

test: all $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; ./$$t; done

clean:
	rm -f $(OBJ) $(BIN) $(MOCK_SO) $(TEST_BINS)

.PHONY: all test clean
