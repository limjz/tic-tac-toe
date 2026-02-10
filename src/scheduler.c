#include "game.h"

void reset_board() {
    // Clear board
    for (int r = 0; r < BOARD_N; r++) {
        for (int c = 0; c < BOARD_N; c++) gameData->board[r][c] = EMPTY_CELL;
    }
    gameData->turn_complete = false;
    gameData->draw = false;

    // Log it
    log_message("Game Reset. New Round starting.");
}


static void build_big_board(char *out, size_t out_sz) {
    // Replace empty cells with '.' for display
    char d[BOARD_N][BOARD_N];
    for (int r = 0; r < BOARD_N; r++) {
        for (int c = 0; c < BOARD_N; c++) {
            char cell = gameData->board[r][c];
            d[r][c] = (cell == 0 || cell == EMPTY_CELL) ? '.' : cell;
        }
    }

    // 4x4 grid labels + big 4x4 board
    // This prints the board in a "bigger" style (multi-line cells).
    snprintf(out, out_sz,
        "\n======= GRID LABELS =======\n\n"
        "   1 |  2 |  3 |  4\n"
        " ----+----+----+----\n"
        "   5 |  6 |  7 |  8\n"
        " ----+----+----+----\n"
        "   9 | 10 | 11 | 12\n"
        " ----+----+----+----\n"
        "  13 | 14 | 15 | 16\n"
        "\n"
        "======= GAME BOARD =======\n\n"
        "     |     |     |     \n"
        "  %c  |  %c  |  %c  |  %c  \n"
        "_____|_____|_____|_____\n"
        "     |     |     |     \n"
        "  %c  |  %c  |  %c  |  %c  \n"
        "_____|_____|_____|_____\n"
        "     |     |     |     \n"
        "  %c  |  %c  |  %c  |  %c  \n"
        "_____|_____|_____|_____\n"
        "     |     |     |     \n"
        "  %c  |  %c  |  %c  |  %c  \n"
        "     |     |     |     \n"
        "\n",
        d[0][0], d[0][1], d[0][2], d[0][3],
        d[1][0], d[1][1], d[1][2], d[1][3],
        d[2][0], d[2][1], d[2][2], d[2][3],
        d[3][0], d[3][1], d[3][2], d[3][3]
    );
}

// server send msg to all player 
static void broadcast_all_locked(const char *msg) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!gameData->player_active[p]) continue;
        int s = gameData->client_sockets[p];
        if (s >= 0) send(s, msg, strlen(msg), 0);
    }
}

void* scheduler_thread(void* arg) {
    (void)arg;
    printf("[Scheduler] Thread started. Waiting for players...\n");

    while (1) {
        pthread_mutex_lock(&gameData->board_mutex);

        if (!gameData->game_active) {
            pthread_mutex_unlock(&gameData->board_mutex);
            break;
        }

        // If round ended (someone won / draw)
        if (gameData->round_over) {
            pthread_mutex_unlock(&gameData->board_mutex);

            log_message("Round Over. Resetting in 5s...");
            sleep(5);

            pthread_mutex_lock(&gameData->board_mutex);
            reset_board();
            gameData->round_over = false;
            gameData->current_turn_id = -1;

            // Broadcast new empty board to everyone
            char screen[2048];
            build_big_board(screen, sizeof(screen));
            broadcast_all_locked(screen);

            pthread_mutex_unlock(&gameData->board_mutex);
            continue;
        }

        // Start when >= MIN_PLAYERS AND all chosen symbols
        bool ready = (gameData->player_count >= MIN_PLAYERS);
        if (ready) {
            int chosen = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (gameData->player_active[i] && gameData->player_symbol[i] != 0) chosen++;
            }
            if (chosen < MIN_PLAYERS) ready = false;
        }

        // First start broadcast
        if (ready && gameData->current_turn_id < 0) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (gameData->player_active[i]) {
                    gameData->current_turn_id = i;
                    break;
                }
            }

            char screen[2048];
            build_big_board(screen, sizeof(screen));

            // Only current player sees "YOUR TURN"
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!gameData->player_active[p]) continue;
                int s = gameData->client_sockets[p];
                if (s < 0) continue;

                if (p == gameData->current_turn_id) {
                    char msg[2600];
                    snprintf(msg, sizeof(msg),
                             "%s>>> YOUR TURN! <<<\nInput next grid number (1-16): ",
                             screen);
                    send(s, msg, strlen(msg), 0);
                } else {
                    char waitmsg[2600];
                    snprintf(waitmsg, sizeof(waitmsg),
                             "%s>>> Waiting for opponent's move... <<<\n",
                             screen);
                    send(s, waitmsg, strlen(waitmsg), 0);
                }
            }
        }

        // After a move, rotate turn and broadcast updated board
        if (gameData->turn_complete && gameData->current_turn_id >= 0) {
            int next = gameData->current_turn_id;
            do {
                next = (next + 1) % MAX_PLAYERS;
            } while (!gameData->player_active[next]);

            gameData->current_turn_id = next;
            gameData->turn_complete = false;

            char screen[2048];
            build_big_board(screen, sizeof(screen));

            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!gameData->player_active[p]) continue;
                int s = gameData->client_sockets[p];
                if (s < 0) continue;

                if (p == gameData->current_turn_id) {
                    char turnmsg[2600];
                    snprintf(turnmsg, sizeof(turnmsg),
                             "%s>>> YOUR TURN! <<<\nInput next grid number (1-16): ",
                             screen);
                    send(s, turnmsg, strlen(turnmsg), 0);
                } else {
                    char waitmsg[2600];
                    snprintf(waitmsg, sizeof(waitmsg),
                             "%s>>> Waiting for opponent's move... <<<\n",
                             screen);
                    send(s, waitmsg, strlen(waitmsg), 0);
                }
            }
        }

        pthread_mutex_unlock(&gameData->board_mutex);
        usleep(100000);
    }

    return NULL;
}
