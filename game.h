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
#include <ctype.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 8080

#define MIN_PLAYERS 3
#define MAX_PLAYERS 3

#define BOARD_N 4       //4x4
#define EMPTY_CELL '.'  

//  logger.c requires these
#define MAX_LOG_LENGTH 256
#define MAX_QUEUE_SIZE 50

struct Game {

    struct { // Scoring
        char name[32];
        int wins;
    } scores[MAX_PLAYERS]; 
    int total_games_played;

    bool round_over;

    // Board (4x4)
    char board[BOARD_N][BOARD_N];   // '.', 'X', 'Y', 'Z'

    // Game state
    bool game_active;
    bool turn_complete;
    int  current_turn_id;           // 0..MAX_PLAYERS-1

    // Players
    int  player_count;
    bool player_active[MAX_PLAYERS];
    int  client_sockets[MAX_PLAYERS];
    char player_symbol[MAX_PLAYERS];     // 'X','Y','Z'
    char player_name[MAX_PLAYERS][32];

    // end state flag used in your code
    bool draw;

    // logger queue fields required by src/logger.c
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
void build_board_string(char *out, size_t out_sz);
void load_scores();
void save_scores();

#endif
