#include "game.h"

static void close_if_dead_socket(int idx) {
    int s = gameData->client_sockets[idx];
    if (s >= 0) {
        close(s);
        gameData->client_sockets[idx] = -1;
    }
}

static void broadcast_all(const char *msg) {
    for (int i = 0; i < 5; i++) {
        if (!gameData->player_active[i]) {
            // if disconnected, ensure parent socket is closed
            close_if_dead_socket(i);
            continue;
        }
        int s = gameData->client_sockets[i];
        if (s < 0) continue;

        ssize_t n = send(s, msg, strlen(msg), 0);
        if (n < 0) {
            // client likely gone; mark inactive and close socket
            gameData->player_active[i] = false;
            close_if_dead_socket(i);
        }
    }
}

// 3-in-a-row win check on 4x4, returns winner number 1..5 else 0
static int check_winner_3(void) {
    // rows
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c <= 1; c++) {
            char a = gameData->boardGame[r][c];
            if (a != 0 &&
                a == gameData->boardGame[r][c+1] &&
                a == gameData->boardGame[r][c+2]) {
                return a - '0';
            }
        }
    }
    // cols
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r <= 1; r++) {
            char a = gameData->boardGame[r][c];
            if (a != 0 &&
                a == gameData->boardGame[r+1][c] &&
                a == gameData->boardGame[r+2][c]) {
                return a - '0';
            }
        }
    }
    // diag down-right
    for (int r = 0; r <= 1; r++) {
        for (int c = 0; c <= 1; c++) {
            char a = gameData->boardGame[r][c];
            if (a != 0 &&
                a == gameData->boardGame[r+1][c+1] &&
                a == gameData->boardGame[r+2][c+2]) {
                return a - '0';
            }
        }
    }
    // diag down-left
    for (int r = 0; r <= 1; r++) {
        for (int c = 2; c < 4; c++) {
            char a = gameData->boardGame[r][c];
            if (a != 0 &&
                a == gameData->boardGame[r+1][c-1] &&
                a == gameData->boardGame[r+2][c-2]) {
                return a - '0';
            }
        }
    }
    return 0;
}

static bool check_draw(void) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (gameData->boardGame[r][c] == 0)
                return false;
    return true;
}

void* scheduler_thread(void* arg) {
    (void)arg;
    printf("[Scheduler] Thread started. Waiting for players...\n");

    while (gameData->player_count < MIN_PLAYERS && gameData->game_active) {
        usleep(200000);
    }

    if (!gameData->game_active) return NULL;

    log_message("Scheduler: Minimum players connected. Game Starting!");
    printf("Scheduler: Minimum players connected. Game Starting!\n");

    // Broadcast initial state
    pthread_mutex_lock(&gameData->board_mutex);
    char boardStr[512];
    build_board_string(boardStr, sizeof(boardStr));
    int turn = gameData->currentPlayer;
    pthread_mutex_unlock(&gameData->board_mutex);

    char startMsg[800];
    snprintf(startMsg, sizeof(startMsg), "Game Starting! Current turn: Player %d\n%s", turn, boardStr);

    pthread_mutex_lock(&gameData->board_mutex);
    broadcast_all(startMsg);
    pthread_mutex_unlock(&gameData->board_mutex);

    while (gameData->game_active) {
        pthread_mutex_lock(&gameData->board_mutex);

        // win/draw check
        int w = check_winner_3();
        if (w != 0) {
            gameData->winner = w;
            gameData->game_active = false;
        } else if (check_draw()) {
            gameData->draw = true;
            gameData->game_active = false;
        }

        if (!gameData->game_active) {
            build_board_string(boardStr, sizeof(boardStr));

            char endMsg[900];
            if (gameData->winner != 0) {
                snprintf(endMsg, sizeof(endMsg), "GAME OVER! Winner: Player %d\n%s", gameData->winner, boardStr);
            } else {
                snprintf(endMsg, sizeof(endMsg), "GAME OVER! Draw.\n%s", boardStr);
            }
            broadcast_all(endMsg);

            pthread_mutex_unlock(&gameData->board_mutex);
            break;
        }

        // Turn pass after move
        if (gameData->turn_complete) {
            int next_player = gameData->currentPlayer;
            int start_player = next_player;

            do {
                next_player = (next_player % 5) + 1; // 1..5
                if (next_player == start_player && !gameData->player_active[next_player - 1]) {
                    break;
                }
            } while (!gameData->player_active[next_player - 1]);

            gameData->currentPlayer = next_player;
            gameData->turn_complete = false;

            build_board_string(boardStr, sizeof(boardStr));

            char msg[900];
            snprintf(msg, sizeof(msg), "Turn passed to Player %d\n%s", next_player, boardStr);
            broadcast_all(msg);

            char logBuf[100];
            snprintf(logBuf, sizeof(logBuf), "Scheduler: Turn passed to Player %d", next_player);
            log_message(logBuf);
        }

        pthread_mutex_unlock(&gameData->board_mutex);
        usleep(100000);
    }

    return NULL;
}
