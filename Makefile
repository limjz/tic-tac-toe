#This file is just to compile 

CC = gcc
CFLAGS = -pthread -Wall -g -I.

all: server client

# The server executable now requires 4 source files
server: server.c src/logger.c src/scheduler.c src/client_handler.c game.h
	$(CC) $(CFLAGS) server.c src/logger.c src/scheduler.c src/client_handler.c src/build_board_string.c -o server -lrt

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client game.logand