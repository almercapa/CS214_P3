CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c99 -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined
LDFLAGS = -fsanitize=address,undefined

.PHONY: all clean test

all: mysh

mysh: mysh.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ mysh.c

test: mysh
	bash tests/run_tests.sh

clean:
	rm -f mysh *.o