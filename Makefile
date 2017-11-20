CC=g++
CFLAGS=-std=c++11 -Wall -Wextra -pedantic -lm -g -pthread

all: popser

popser: popser.cpp
	$(CC) $(CFLAGS) popser.cpp -o popser

clean:
	rm popser
