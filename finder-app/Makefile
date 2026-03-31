CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -g

# Default target
all: writer

# Build the writer application
writer: writer.c
	$(CC) $(CFLAGS) -o writer writer.c

# Clean target
clean:
	rm -f writer *.o

.PHONY: all clean
