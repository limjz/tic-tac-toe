#ifndef GAME_H
#define GAME_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 8080
#define MAX_LOG_LENGTH 256
#define MAX_QUEUE_SIZE 50
#define MIN_PLAYERS 3

struct Game {
    char boardGame[4][4];
    int currentPlayer;            // human player number: 1..5
    int player_count;
    bool game_active;

    bool player_active[5];
    int  client_sockets[5];       // <-- needed for broadcast
    bool turn_complete;

    int winner;                   // 0 = none, else 1..5
    bool draw;

    char log_queue[MAX_QUEUE_SIZE][MAX_LOG_LENGTH];
    int log_head;
    int log_tail;

    pthread_mutex_t board_mutex;
    pthread_mutex_t log_mutex;
};

extern struct Game *gameData;
extern const char* SHM_NAME;
extern const size_t SHM_SIZE;

void log_message(char *msg);
void* scheduler_thread(void* arg);
void* logger_thread(void* arg);
void handle_client(int client_socket, int player_id, int human_player_number);
void signal_handler(int signo);

#endif
