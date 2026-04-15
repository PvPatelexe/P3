#Standard flags from Project 1
CC = gcc
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined

all: mysh

#linking the executable
mysh: mysh.o
	$(CC) $(CFLAGS) -o $@ $^
#We did this in all our projects
%.o: %.c
	$(CC) $(CFLAGS) -c $<