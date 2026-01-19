CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11 -pedantic -D_POSIX_C_SOURCE=200809L
INCLUDES = -Iinclude

SRC = src/main.c src/model.c src/parser.c src/rng.c src/term.c
OBJ = $(SRC:.c=.o)
BIN = bin/cram

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
