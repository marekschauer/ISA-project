CC=g++
CFLAGS=-std=c++11 -Wall -Wextra -pedantic -lm -g -pthread

all: popser

popser: popser.c
	$(CC) $(CFLAGS) popser.c -o popser

clean:
	rm popser
