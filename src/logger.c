#include "game.h"

// Helper function used by other files to add logs
void log_message(char *msg) {
    if (gameData == NULL) return;

    pthread_mutex_lock(&gameData->log_mutex);
    int next_tail = (gameData->log_tail + 1) % MAX_QUEUE_SIZE;
    if (next_tail != gameData->log_head) { 
        strncpy(gameData->log_queue[gameData->log_tail], msg, MAX_LOG_LENGTH - 1);
        gameData->log_queue[gameData->log_tail][MAX_LOG_LENGTH - 1] = '\0';
        gameData->log_tail = next_tail;
    }
    pthread_mutex_unlock(&gameData->log_mutex);
}

// The Consumer Thread
void* logger_thread(void* arg) {
    printf("[Logger] Thread started.\n");
    FILE *fp = fopen("game.log", "w");
    if (!fp) { perror("Failed to open game.log"); return NULL; }

    fprintf(fp, "Server Started. Logger Initialized.\n");

    while (gameData->game_active) {
        pthread_mutex_lock(&gameData->log_mutex);
        while (gameData->log_head != gameData->log_tail) {
            fprintf(fp, "%s\n", gameData->log_queue[gameData->log_head]);
            gameData->log_head = (gameData->log_head + 1) % MAX_QUEUE_SIZE;
        }
        fflush(fp); 
        pthread_mutex_unlock(&gameData->log_mutex);
        usleep(100000); 
    }
    fclose(fp);
    return NULL;
}