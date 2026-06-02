CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -g
TARGET ?= aesdsocket
LDFLAGS ?= -pthread -lrt

# Default and all targets
default: all
all: aesdsocket

# Build the aesdsocket application
aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c $(LDFLAGS)

# Clean target
clean:
	rm -f aesdsocket *.o

.PHONY: default all clean
