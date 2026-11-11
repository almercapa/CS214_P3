CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c99 -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined
LDFLAGS = -fsanitize=address,undefined

OBJS = mysh.o

.PHONY: all clean

all: mysh

mysh: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f mysh *.o