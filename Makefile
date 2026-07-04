CC ?= cc
CFLAGS ?= -std=c23 -Wall -Wextra -O3 -flto -march=native

SRCS = $(filter-out src/b1cc_token_lexer.c, $(wildcard src/*.c))
OBJS = $(SRCS:src/%.c=build/%.o)

all: build/b1cc

build/b1cc: $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(OBJS) -o $@

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

test: build/b1cc
	./test.sh

clean:
	rm -rf build

.PHONY: all test clean
