#include "game.h"

static void send_str(int sock, const char *s) {
    send(sock, s, strlen(s), 0);
}

static void trim(char *s) {
    s[strcspn(s, "\r\n")] = 0;
}

static int grid_to_rc(int grid, int *r, int *c) {
    if (grid < 1 || grid > 9) return 0;
    int idx = grid - 1;
    *r = idx / 3;
    *c = idx % 3;
    return 1;
}

static int check_win(char b[3][3], char sym) {
    for (int r = 0; r < 3; r++)
        if (b[r][0] == sym && b[r][1] == sym && b[r][2] == sym) return 1;
    for (int c = 0; c < 3; c++)
        if (b[0][c] == sym && b[1][c] == sym && b[2][c] == sym) return 1;
    if (b[0][0] == sym && b[1][1] == sym && b[2][2] == sym) return 1;
    if (b[0][2] == sym && b[1][1] == sym && b[2][0] == sym) return 1;
    return 0;
}

static int check_draw(char b[3][3]) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (b[r][c] == EMPTY_CELL) return 0;
    return 1;
}

void handle_client(int client_socket, int player_id, int human_player_number) {
    char buf[BUFFER_SIZE];

    // Name
    send_str(client_socket, "Enter your name: ");
    int n = recv(client_socket, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(client_socket); exit(0); }
    buf[n] = 0; trim(buf);

    pthread_mutex_lock(&gameData->board_mutex);
    snprintf(gameData->player_name[player_id], sizeof(gameData->player_name[player_id]), "%s", buf);
    pthread_mutex_unlock(&gameData->board_mutex);

    // Symbol X/Y/Z
    while (1) {
        send_str(client_socket, "Choose your symbol (X/Y/Z): ");
        n = recv(client_socket, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { close(client_socket); exit(0); }
        buf[n] = 0; trim(buf);

        char sym = (char)toupper((unsigned char)buf[0]);
        if (!(sym == 'X' || sym == 'Y' || sym == 'Z')) {
            send_str(client_socket, "Invalid symbol. Choose X, Y, or Z.\n");
            continue;
        }

        pthread_mutex_lock(&gameData->board_mutex);
        bool taken = false;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (gameData->player_active[i] && gameData->player_symbol[i] == sym) {
                taken = true; break;
            }
        }
        if (!taken) gameData->player_symbol[player_id] = sym;
        pthread_mutex_unlock(&gameData->board_mutex);

        if (taken) {
            send_str(client_socket, "That symbol is taken. Choose another.\n");
            continue;
        }

        char ok[128];
        snprintf(ok, sizeof(ok), "Your symbol has been assigned: %c\nWaiting for game to start...\n", sym);
        send_str(client_socket, ok);
        break;
    }

    // Moves (grid number 1-9)
    while (1) {
        int bytes = recv(client_socket, buf, sizeof(buf) - 1, 0);
        if (bytes <= 0) {
            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[player_id] = false;
            pthread_mutex_unlock(&gameData->board_mutex);
            close(client_socket);
            exit(0);
        }
        buf[bytes] = 0;
        trim(buf);

        pthread_mutex_lock(&gameData->board_mutex);

        if (!gameData->game_active) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Game ended.\n");
            break;
        }

        if (gameData->current_turn_id < 0) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Waiting for game to start...\n");
            continue;
        }

        if (gameData->current_turn_id != player_id || gameData->turn_complete) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, ">>> Waiting for opponent's move... <<<\n");
            continue;
        }

        int grid = atoi(buf);
        int r, c;
        if (!grid_to_rc(grid, &r, &c)) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Invalid input. Input next grid number (1-9): ");
            continue;
        }

        if (gameData->board[r][c] != EMPTY_CELL) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Invalid move. Spot taken.\nInput next grid number (1-9): ");
            continue;
        }

        char sym = gameData->player_symbol[player_id];
        gameData->board[r][c] = sym;

        bool win = check_win(gameData->board, sym);
        bool draw = (!win && check_draw(gameData->board));

        if (win) {
            strcpy(gameData->scores[player_id].name, gameData->player_name[player_id]);
            gameData->scores[player_id].wins++;

            char msg[256];
            snprintf(msg, sizeof(msg), ">>> PLAYER %d WINS! <<<\nTotal Wins: %d\nRestarting in 5 seconds...\n", 
                human_player_number, gameData->scores[player_id].wins);

                send_str(client_socket, msg);
        } else if (draw) {
            send_str(client_socket, ">>> DRAW! <<<\nRestarting in 5 seconds...\n");
        }

        gameData->turn_complete = true;

        pthread_mutex_unlock(&gameData->board_mutex);

        if (win) {
            char msg[128];
            snprintf(msg, sizeof(msg), ">>> PLAYER %d (%c) WINS! <<<\n", human_player_number, sym);
            send_str(client_socket, msg);
        } else if (draw) {
            send_str(client_socket, ">>> DRAW! <<<\n");
        }
    }

    close(client_socket);
    exit(0);
}
