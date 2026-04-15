CC = gcc
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined

# build needed
all: mysh

# linker
mysh: mysh.o input.o parser.o built_in.o execute.o
	$(CC) $(CFLAGS) -o $@ $^

# The other files
mysh.o: mysh.h
input.o: mysh.h
parser.o: mysh.h
built_in.o: mysh.h
execute.o: mysh.h

# generic to all make
%.o: %.c
	$(CC) $(CFLAGS) -c $<