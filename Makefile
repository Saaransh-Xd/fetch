CC = gcc

CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2

SRC = src/main.c src/ansi.c src/macos.c

TARGET = bin/fetch

all:
	mkdir -p bin
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
