CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c99 -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined
LDFLAGS = -fsanitize=address,undefined

EXEC_OBJ = exec_stub.o
OBJS = mysh.o input.o parse.o glob.o arraylist.o $(EXEC_OBJ)

.PHONY: all clean

all: mysh

mysh: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

$(OBJS): job.h
mysh.o input.o: input.h
mysh.o parse.o: parse.h
parse.o glob.o arraylist.o: arraylist.h
parse.o glob.o: glob.h
mysh.o $(EXEC_OBJ): exec.h

clean:
	rm -f mysh *.o