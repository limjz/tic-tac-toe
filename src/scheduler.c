#include "game.h"

static void build_big_board(char *out, size_t out_sz) {
    // Convert board (3x3) to a big ASCII board like the reference
    // Uses gameData->board[r][c] as '.', 'X','Y','Z'

    char a = gameData->board[0][0];
    char b = gameData->board[0][1];
    char c = gameData->board[0][2];
    char d = gameData->board[1][0];
    char e = gameData->board[1][1];
    char f = gameData->board[1][2];
    char g = gameData->board[2][0];
    char h = gameData->board[2][1];
    char i = gameData->board[2][2];

    snprintf(out, out_sz,
        "=== GRID LABELS ===\n"
        "  1 | 2 | 3\n"
        " ---+---+---\n"
        "  4 | 5 | 6\n"
        " ---+---+---\n"
        "  7 | 8 | 9\n"
        "\n"
        "=== GAME BOARD ===\n"
        "    |   |   \n"
        "  %c | %c | %c \n"
        "____|___|____\n"
        "    |   |   \n"
        "  %c | %c | %c \n"
        "____|___|____\n"
        "    |   |   \n"
        "  %c | %c | %c \n"
        "    |   |   \n"
        "\n",
        a, b, c,
        d, e, f,
        g, h, i
    );
}

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

        // Start when >=3 players AND all have chosen symbols
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
                if (gameData->player_active[i]) { gameData->current_turn_id = i; break; }
            }

            char screen[2048];
            build_big_board(screen, sizeof(screen));

            char msg[2600];
            snprintf(msg, sizeof(msg),
                     "%s>>> YOUR TURN! <<<\nInput next grid number (1-9): ",
                     screen);
            // Only current player should see "YOUR TURN", others see waiting
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!gameData->player_active[p]) continue;
                int s = gameData->client_sockets[p];
                if (s < 0) continue;

                if (p == gameData->current_turn_id) {
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
                             "%s>>> YOUR TURN! <<<\nInput next grid number (1-9): ",
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

void reset_board() {
    // Clear board
    for (int r=0; r<BOARD_N; r++) {
        for (int c=0; c<BOARD_N; c++) gameData->board[r][c] = EMPTY_CELL;
    }
    gameData->turn_complete = false;
    gameData->draw = false;
    // Don't set game_active = false!
    
    // Log it
    log_message("Game Reset. New Round starting.");
}
