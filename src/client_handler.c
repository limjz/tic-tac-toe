#include "game.h"

void handle_client(int client_socket, int player_id, int human_player_number) {
    printf("Player %d connected (ID: %d).\n", human_player_number, player_id);

    char logBuf[100];
    snprintf(logBuf, sizeof(logBuf), "Player %d connected.", human_player_number);
    log_message(logBuf);

    char msg_from_client[BUFFER_SIZE];

    // Welcome + show initial board
    char boardStr[512];
    pthread_mutex_lock(&gameData->board_mutex);
    build_board_string(boardStr, sizeof(boardStr));
    int cur = gameData->currentPlayer;
    pthread_mutex_unlock(&gameData->board_mutex);

    char welcome[900];
    snprintf(welcome, sizeof(welcome),
             "Welcome! Waiting for %d players to start...\nCurrent turn: Player %d\n%s",
             MIN_PLAYERS, cur, boardStr);
    send(client_socket, welcome, strlen(welcome), 0);

    while (1) {
        memset(msg_from_client, 0, BUFFER_SIZE);
        int bytes = recv(client_socket, msg_from_client, BUFFER_SIZE, 0);

        if (bytes <= 0) {
            printf("Player %d disconnected.\n", human_player_number);

            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[player_id] = false;
            // scheduler will close client_sockets[player_id] in parent
            pthread_mutex_unlock(&gameData->board_mutex);

            snprintf(logBuf, sizeof(logBuf), "Player %d disconnected.", human_player_number);
            log_message(logBuf);
            break;
        }

        msg_from_client[strcspn(msg_from_client, "\n")] = 0;

        pthread_mutex_lock(&gameData->board_mutex);
        bool ended = !gameData->game_active;
        int current_turn = gameData->currentPlayer;
        bool my_turn = (current_turn == human_player_number && !gameData->turn_complete);
        build_board_string(boardStr, sizeof(boardStr));
        pthread_mutex_unlock(&gameData->board_mutex);

        if (ended) {
            send(client_socket, "GAME OVER.\n", strlen("GAME OVER.\n"), 0);
            break;
        }

        if (!my_turn) {
            char wait_msg[900];
            snprintf(wait_msg, sizeof(wait_msg),
                     "It is not your turn. Please wait...\nCurrent turn: Player %d\n%s",
                     current_turn, boardStr);
            send(client_socket, wait_msg, strlen(wait_msg), 0);
            continue;
        }

        // Process Move: expects "row col"
        int row, col;
        if (sscanf(msg_from_client, "%d %d", &row, &col) != 2) {
            char err[900];
            snprintf(err, sizeof(err),
                     "Invalid format. Enter: ROW COL (e.g., 0 0)\n%s",
                     boardStr);
            send(client_socket, err, strlen(err), 0);
            continue;
        }

        pthread_mutex_lock(&gameData->board_mutex);

        if (row < 0 || row > 3 || col < 0 || col > 3 || gameData->boardGame[row][col] != 0) {
            build_board_string(boardStr, sizeof(boardStr));
            pthread_mutex_unlock(&gameData->board_mutex);

            char err[900];
            snprintf(err, sizeof(err),
                     "Invalid move! Spot taken or out of bounds.\n%s",
                     boardStr);
            send(client_socket, err, strlen(err), 0);
            continue;
        }

        // Place move
        gameData->boardGame[row][col] = (char)(human_player_number + '0');
        gameData->turn_complete = true;

        // Build updated board for the mover to see immediately
        build_board_string(boardStr, sizeof(boardStr));

        pthread_mutex_unlock(&gameData->board_mutex);

        char confirm[900];
        snprintf(confirm, sizeof(confirm),
                 "Move accepted! You placed at (%d,%d)\n%s"
                 "Wait for next turn...\n",
                 row, col, boardStr);
        send(client_socket, confirm, strlen(confirm), 0);

        snprintf(logBuf, sizeof(logBuf), "Player %d placed at %d,%d", human_player_number, row, col);
        log_message(logBuf);
    }

    close(client_socket);
    exit(0);
}
