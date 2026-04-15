CC = gcc
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined

# build needed
all: mysh

# linker
mysh: mysh.o input.o parse.o builtins.o execute.o
	$(CC) $(CFLAGS) -o $@ $^

# The other files
mysh.o: mysh.h
input.o: mysh.h
parse.o: mysh.h
builtins.o: mysh.h
execute.o: mysh.h

# generic to all make
%.o: %.c
	$(CC) $(CFLAGS) -c $<