CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -g

# Default and all targets
default: all
all: aesdsocket

# Build the aesdsocket application
aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

# Clean target
clean:
	rm -f aesdsocket *.o

.PHONY: default all clean
