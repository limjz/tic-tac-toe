# Compiler and Flags
CC = gcc
CFLAGS = -pthread -Wall -g # -g adds debug info, -pthread is required

# Targets
all: server client

# Compile Server (Links with Real-time library for Shared Memory)
server: server.c
	$(CC) $(CFLAGS) server.c -o server -lrt

# Compile Client
client: client.c
	$(CC) $(CFLAGS) client.c -o client

# Clean up binaries
clean:
	rm -f server client game.log