CC       = cc
CFLAGS   = -Wall -Wextra -std=c11 -Iinclude -O2
SRC      = src/lexer.c src/arena.c src/diag.c src/parser.c src/typecheck.c src/codegen.c src/main.c
OBJ      = $(SRC:.c=.o)
BIN      = rinc

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean