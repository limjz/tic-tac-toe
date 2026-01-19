#include "game.h"

void* scheduler_thread(void* arg) {
    printf("[Scheduler] Thread started. Waiting for players...\n");

    // Wait for players
    while (gameData->player_count < MIN_PLAYERS && gameData->game_active) {
        usleep(1000000);
    }
    
    if (!gameData->game_active) return NULL;

    log_message("Scheduler: Minimum players connected. Game Starting!");
    printf("Scheduler: Minimum players connected. Game Starting!\n");

    while (gameData->game_active) {
        pthread_mutex_lock(&gameData->board_mutex);
        
        // Round Robin Logic
        int max_possible_id = 5;

            do {
                next_player = (next_player % max_possible_id) + 1;
                // Safety check for active players
                if (next_player == start_player && !gameData->player_active[next_player - 1]) {
                    break; 
                }
            } while (!gameData->player_active[next_player - 1]);

            gameData->currentPlayer = next_player;
            gameData->turn_complete = false; 
            
            char logBuf[100];
            snprintf(logBuf, sizeof(logBuf), "Scheduler: Turn passed to Player %d", next_player);
            log_message(logBuf);
        
        pthread_mutex_unlock(&gameData->board_mutex);
        usleep(100000); 
    }
    return NULL;
}