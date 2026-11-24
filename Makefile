# ratslap-g305x — build and test
#
# `make`       build the CLI binary
# `make test`  unit tests + mock-hidraw integration test (no mouse needed)
#
# The layout is deliberately flat (krayon/ratslap style): makepkg stages
# local sources into ./src by file name only, so nested directories would
# collide with makepkg's own work directories. Do not reintroduce src/.

CC       ?= cc
CFLAGS   ?= -O2
WARN      = -std=c99 -Wall -Wextra -Werror
DEFS      = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
ALL_CFLAGS = $(WARN) $(DEFS) $(CFLAGS)

BIN      = ratslap-g305x
HDRS     = util.h hidpp.h onboard.h actions.h
LIB_SRC  = hidpp.c onboard.c actions.c
OBJ      = $(LIB_SRC:.c=.o) main.o

TEST_BINS = test_crc test_hidpp test_profile test_actions
MOCK_SO   = mock_hidraw.so

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJ)

%.o: %.c $(HDRS)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

# golden.h is committed. Regenerate only when it is missing
# (`rm golden.h && make`) so `make test` never needs python3 in a
# clean build environment.
golden.h: gen_golden.py
	@test -f $@ || python3 gen_golden.py > $@

test_%: test_%.c $(LIB_SRC) $(HDRS) golden.h
	$(CC) $(ALL_CFLAGS) -o $@ test_$*.c $(LIB_SRC)

test: all $(MOCK_SO) $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; ./$$t; done; \
	echo "== integration"; bash integration.sh

mock_hidraw.so: mock_hidraw.c golden.h
	$(CC) -std=gnu99 -Wall -Wextra -Werror -shared -fPIC -o $@ mock_hidraw.c -ldl

clean:
	rm -f $(OBJ) $(BIN) $(MOCK_SO) $(TEST_BINS)

PREFIX ?= /usr/local

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 42-g305x.rules $(DESTDIR)/usr/lib/udev/rules.d/42-g305x.rules
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/ratslap-g305x/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/ratslap-g305x/LICENSE

.PHONY: all test clean install
