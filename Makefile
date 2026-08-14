CC = gcc

CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2

SRC = src/main.c src/ansi.c src/macos.c larp/embedded.c

PYTHON ?= python3
PYTHON_CONFIG ?= python3-config
PYTHON_CFLAGS = $(shell $(PYTHON_CONFIG) --includes 2>/dev/null)
PYTHON_LDFLAGS = $(shell $(PYTHON_CONFIG) --embed --ldflags 2>/dev/null)

ifeq ($(strip $(PYTHON_CFLAGS)),)
$(error CPython development files not found; install python3-dev and python3-config)
endif

TARGET = bin/fetch

all:
	mkdir -p bin
	$(CC) $(CFLAGS) $(PYTHON_CFLAGS) $(SRC) $(PYTHON_LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
