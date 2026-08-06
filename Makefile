CC = gcc

CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2

SRC = src/main.c src/ansi.c

TARGET = bin/fetch

all:
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
