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

// Constants
#define BUFFER_SIZE 1024
#define SERVER_PORT 8080
#define MAX_LOG_LENGTH 256
#define MAX_QUEUE_SIZE 50
#define MIN_PLAYERS 3

// Data Structures
struct Game {
    char boardGame[4][4]; 
    int currentPlayer;  
    int player_count; 
    bool game_active;   

    bool player_active[5]; 
    bool turn_complete; 

    char log_queue[MAX_QUEUE_SIZE][MAX_LOG_LENGTH];
    int log_head;
    int log_tail;

    pthread_mutex_t board_mutex; 
    pthread_mutex_t log_mutex; 
};

// Global Variables (Extern)
extern struct Game *gameData;
extern const char* SHM_NAME;
extern const size_t SHM_SIZE;

// Function Prototypes
void log_message(char *msg);
void* scheduler_thread(void* arg);
void* logger_thread(void* arg);
void handle_client(int client_socket, int player_id, int human_player_number);
void signal_handler(int signo);

#endif