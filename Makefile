CC = gcc
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined

# build needed
all: mysh

# linker
mysh: main.o input.o parse.o builtins.o execute.o
	$(CC) $(CFLAGS) -o $@ $^

# The other files
main.o: mysh.h
input.o: mysh.h
parse.o: mysh.h
builtins.o: mysh.h
execute.o: mysh.h

# generic to all make
%.o: %.c
	$(CC) $(CFLAGS) -c $<