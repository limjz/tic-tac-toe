#include "game.h"

void handle_client(int client_socket, int player_id, int human_player_number) {
    printf("Player %d connected (ID: %d).\n", human_player_number, player_id);
    
    char logBuf[100];
    snprintf(logBuf, sizeof(logBuf), "Player %d connected.", human_player_number);
    log_message(logBuf);

    char msg_from_client[BUFFER_SIZE]; 
    char *welcome_msg = "Welcome! Waiting for 3 players to start...\n"; 
    send(client_socket, welcome_msg, strlen(welcome_msg), 0);
 
    while (1) {
        memset(msg_from_client, 0, BUFFER_SIZE);
        int bytes = recv(client_socket, msg_from_client, BUFFER_SIZE, 0);

        if (bytes <= 0) {
            printf("Player %d disconnected.\n", human_player_number);
            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[player_id] = false; 
            pthread_mutex_unlock(&gameData->board_mutex);
            
            snprintf(logBuf, sizeof(logBuf), "Player %d disconnected.", human_player_number);
            log_message(logBuf);
            break;
        }
        
        msg_from_client[strcspn(msg_from_client, "\n")] = 0;

        // Check Turn
        bool my_turn = false;
        pthread_mutex_lock(&gameData->board_mutex);
        if (gameData->currentPlayer == human_player_number && !gameData->turn_complete) {
            my_turn = true;
        }
        pthread_mutex_unlock(&gameData->board_mutex);

        if (!my_turn) {
            char *wait_msg = "It is not your turn. Please wait...\n";
            send(client_socket, wait_msg, strlen(wait_msg), 0);
            continue; 
        }

        // Process Move
        int row, col;
        if (sscanf(msg_from_client, "%d %d", &row, &col) != 2) {
            char *err = "Invalid format. Enter: ROW COL (e.g., 0 0)\n";
            send(client_socket, err, strlen(err), 0);
            continue; 
        }

        pthread_mutex_lock(&gameData->board_mutex);
        if (row < 0 || row > 3 || col < 0 || col > 3 || gameData->boardGame[row][col] != 0) {
            pthread_mutex_unlock(&gameData->board_mutex);
            char *err = "Invalid move! Spot taken or out of bounds.\n";
            send(client_socket, err, strlen(err), 0);
            continue;
        }

        // Place Move
        gameData->boardGame[row][col] = human_player_number + '0'; 
        gameData->turn_complete = true; 
        pthread_mutex_unlock(&gameData->board_mutex);

        char boardStr[512];
        char confirm[600];
        snprintf(confirm, sizeof(confirm), "Move accepted!\n%sWait for next turn...\n", boardStr);
        send(client_socket, confirm, strlen(confirm), 0);

        snprintf(logBuf, sizeof(logBuf), "Player %d placed at %d,%d", human_player_number, row, col);
        log_message(logBuf);
    }
    close(client_socket);
    exit(0);
}