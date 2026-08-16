CC       = cc
CFLAGS   = -Wall -Wextra -std=c11 -Iinclude -O2
SRC      = src/lexer.c src/arena.c src/diag.c src/parser.c src/typecheck.c src/codegen.c src/main.c
OBJ      = $(SRC:.c=.o)
DEPS     = $(wildcard include/rin/*.h)
BIN      = rinc
PREFIX   = /usr/local

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BIN)
	install -d $(PREFIX)/bin
	install -m 755 $(BIN) $(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(PREFIX)/bin/$(BIN)

examples: $(BIN)
	@for f in examples/*.rn; do \
		echo "-- $$f --"; \
		./$(BIN) $$f -conv /tmp/$$(basename $$f .rn) || exit 1; \
	done

clean:
	rm -f $(OBJ) $(BIN)
	rm -f *.qbe *.s *.rt.c

.PHONY: all clean install uninstall examples