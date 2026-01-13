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

#define BUFFER_SIZE 1024
#define SERVER_PORT 8080
#define MAX_LOG_LENGTH 256
#define MAX_QUEUE_SIZE 50
#define MIN_PLAYERS 3

/* Shared memory constants */
struct Game
{
    char boardGame[4][4]; //board game 4x4
    int currentPlayer;  //track current player's turn
    bool game_active;   
    int player_count; //number of connected players

    // -- Scheduler system ---
    bool player_active[5]; //track active players
    bool turn_complete; //true if done, thn scheduler reset to false

    // --- Logging system ---
    char log_queue[MAX_QUEUE_SIZE][MAX_LOG_LENGTH];
    int log_head;
    int log_tail;

    // --- Mutexes for synchronization ---
    pthread_mutex_t board_mutex; //mutex for synchronizing access to the game board
    pthread_mutex_t log_mutex; //mutex for synchronizing access to the log file
};

struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);
char server_message[BUFFER_SIZE];
int PLAYER_IDS[3] = {0, 1, 2};

void log_message(char *msg);

/* Thread functions */  
void* scheduler_thread(void* arg) {
    printf("[Scheduler] Thread started. Waiting for players...\n");

    // Wait until at least 3 players connect before managing turns
    while (gameData->player_count < MIN_PLAYERS && gameData->game_active) {
        usleep(1000000);
    }
    
    if (!gameData->game_active) {
        pthread_mutex_lock(&gameData->board_mutex);
    }

    log_message("Scheduler: Minimum players connected. Game Starting!");
    printf("Scheduler: Minimum players connected. Game Starting!\n");

    while (gameData->game_active) {
        pthread_mutex_lock(&gameData->board_mutex);

        // 1. Check if the current player finished their turn
        if (gameData->turn_complete) {
            
            // Calculate next player (Round Robin)
            int next_player = gameData->currentPlayer;
            int start_player = next_player;

            do {
                next_player = (next_player % gameData->player_count) + 1; // 1->2->3->1...
                
                // Logic to handle inactive players (optional but recommended by rubric)
                // Assuming you map Player 1 -> index 0 in player_active
                if (next_player == start_player && !gameData->player_active[next_player - 1]) {
                     break;
                }
            } while (!gameData->player_active[next_player - 1]);

            // Update state
            gameData->currentPlayer = next_player;
            gameData->turn_complete = false; // Reset flag for the new player
            
            char logBuf[100];
            sprintf(logBuf, "Scheduler: Turn passed to Player %d", sizeof(logBuf)),
            log_message(logBuf);
        }

        pthread_mutex_unlock(&gameData->board_mutex);
        usleep(100000); // Check 10 times a second
    }
    return NULL;
}

void* logger_thread(void* arg) {
    printf("[Logger] Thread started.\n");
    FILE *fp = fopen("game.log", "w");
    if (!fp) {
        perror("Failed to open game.log");
        return NULL;
    }

    fprintf(fp, "Server Started. Logger Initialized.\n");

    while (gameData->game_active) {
        pthread_mutex_lock(&gameData->log_mutex);
        
        // Check if there is data in the circular buffer
        while (gameData->log_head != gameData->log_tail) {
            // Write to file
            fprintf(fp, "%s\n", gameData->log_queue[gameData->log_head]);

            
            // Advance head
            gameData->log_head = (gameData->log_head + 1) % MAX_QUEUE_SIZE;
        }
        fflush(fp); // Ensure it's written immediately [cite: 50]
        
        pthread_mutex_unlock(&gameData->log_mutex);
        usleep(100000); // Sleep 0.1s to avoid high CPU usage
    }
    
    fclose(fp);
    return NULL;
}

void log_message(char *msg) {

    if (gameData == NULL) return;

    pthread_mutex_lock(&gameData->log_mutex);
    
    // Add message to queue at 'tail'
    int next_tail = (gameData->log_tail + 1) % MAX_QUEUE_SIZE;
    if (next_tail != gameData->log_head) { // Check if full
        strncpy(gameData->log_queue[gameData->log_tail], msg, MAX_LOG_LENGTH - 1);
        gameData->log_queue[gameData->log_tail][MAX_LOG_LENGTH - 1] = '\0'; // Safety null
        gameData->log_tail = next_tail;
    }
    
    pthread_mutex_unlock(&gameData->log_mutex);
}

void handle_client (int client_socket, int player_id, int player_count)
{
    // Handle client logic here 
    printf ("Player %d connected. Current Player: %d\n", player_id + 1, player_count);
    
    char logBuf[100];
    snprintf(logBuf, sizeof(logBuf), "Player %d connected.", player_id + 1);
    log_message(logBuf);

    char msg_from_client[BUFFER_SIZE]; // message send from client
    char *msg = "Welcome to the server !!"; 
    send (client_socket, msg, strlen(msg), 0);
 
    while (1)
    {
        memset (msg_from_client, 0, BUFFER_SIZE); //refresh the buffer container 
        int bytes = recv (client_socket, msg_from_client, BUFFER_SIZE, 0); 

        if (bytes <= 0)
        {
            printf("Player %d disconnected. \n", player_id + 1);

            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[player_id] = false; // Mark player as inactive
            pthread_mutex_unlock(&gameData->board_mutex);

            snprintf(logBuf, sizeof(logBuf), "Player %d disconnected.", player_id - 1);
            log_message(logBuf);
            break;
        }


        printf ("Player %d send %s\n", player_id + 1, msg_from_client);
    }
    close(client_socket);
    exit(0);

}

void signal_handler(int signo)
{
    // Reap zombie processes [cite: 34]
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main() {
    // 1. Clean up old shared memory
    shm_unlink(SHM_NAME); 

    // 2. Create and open shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); 
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(1);
    }
    
    // 3. Set size of shared memory
    ftruncate(shm_fd, SHM_SIZE);

    // 4. Map shared memory
    gameData = (struct Game*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); 
    if (gameData == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // ==========================================
    // DATA INITIALIZATION (CRITICAL UPDATES)
    // ==========================================
    gameData->player_count = 0;
    gameData->game_active = true;
    gameData->currentPlayer = 1; // Start with Player 1 (Index 0 in active array usually)
    gameData->turn_complete = false; // NEW: No moves made yet

    // NEW: Initialize Logger Pointers
    gameData->log_head = 0;
    gameData->log_tail = 0;

    // NEW: Initialize Player Active Array
    for(int i = 0; i < 3; i++) {
        gameData->player_active[i] = false;
    }

    // ==========================================
    // MUTEX INITIALIZATION
    // ==========================================
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // CRITICAL: This allows the mutex to work across fork() processes
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&gameData->board_mutex, &attr);
    pthread_mutex_init(&gameData->log_mutex, &attr); // NEW: Init Log Mutex

    // 5. Create Internal Threads
    pthread_t scheduler, logger;
    pthread_create(&scheduler, NULL, scheduler_thread, NULL);
    pthread_create(&logger, NULL, logger_thread, NULL);
    
    // 6. Socket Setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        exit(1);
    }

    if(bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Binding failed");
        exit(1);
    }

    listen(server_fd, 5);
    signal(SIGCHLD, signal_handler); 

    printf("Server started. Waiting for players...\n");

    // 7. Accept Loop
    int player_count = 0;
    while (player_count < MIN_PLAYERS || gameData->game_active) { // Or whatever limit you want
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int new_client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        
        if (new_client_fd == -1) {
             // Handle shutdown gracefully if needed
            if (!gameData->game_active) break;
            perror("Accept failed");
            continue;
        }
        
        // NEW: Mark player as active before forking
        // Note: Check bounds in real code to avoid overflow
pthread_mutex_lock(&gameData->board_mutex);
        if (player_count < 3) {
            gameData->player_active[player_count] = true;
            gameData->player_count++;
        }
        pthread_mutex_unlock(&gameData->board_mutex);

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
        } 
        else if (pid == 0) {
            // === CHILD PROCESS ===
            close(server_fd); 
            // Child handles the client
            handle_client(new_client_fd, player_count, player_count + 1);
            exit(0);
        } 
        else {
            // === PARENT PROCESS ===
            close(new_client_fd);
            printf("Client connected (PID: %d)\n", pid);
            player_count++;
        }
    }

    // 8. Cleanup & Wait
    pthread_join(scheduler, NULL);
    pthread_join(logger, NULL);

    close(server_fd);
    munmap(gameData, SHM_SIZE);
    shm_unlink(SHM_NAME);
    kill(0, SIGTERM);

    return 0;
}