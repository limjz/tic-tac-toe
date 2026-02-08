#include "game.h"

/* ---------- helpers ---------- */

static void trim_newline(char *s) {
    if (!s) return;
    s[strcspn(s, "\r\n")] = '\0';
}

static void send_str(int sock, const char *s) {
    if (!s) return;
    send(sock, s, strlen(s), 0);
}

static void send_prompt(int sock, int max_cell) {
    char p[80];
    snprintf(p, sizeof(p), "Input next grid number (1-%d): ", max_cell);
    send_str(sock, p);
}

static int parse_grid_number(const char *msg, int *out_r, int *out_c) {
    // accepts: "7"  (grid number)
    // you can extend later to accept "row col" if needed
    int idx;
    if (sscanf(msg, "%d", &idx) != 1) return 0;

    int max_cell = BOARD_N * BOARD_N;
    if (idx < 1 || idx > max_cell) return 0;

    idx -= 1; // make 0-based
    *out_r = idx / BOARD_N;
    *out_c = idx % BOARD_N;
    return 1;
}

static int symbol_taken(char sym) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gameData->player_active[i] && gameData->player_symbol[i] == sym) return 1;
    }
    return 0;
}

/* 4x4 win: any full row/col, or two diagonals */
static int check_win(char b[BOARD_N][BOARD_N], char sym) {
    // rows
    for (int r = 0; r < BOARD_N; r++) {
        int ok = 1;
        for (int c = 0; c < BOARD_N; c++) {
            if (b[r][c] != sym) { ok = 0; break; }
        }
        if (ok) return 1;
    }

    // cols
    for (int c = 0; c < BOARD_N; c++) {
        int ok = 1;
        for (int r = 0; r < BOARD_N; r++) {
            if (b[r][c] != sym) { ok = 0; break; }
        }
        if (ok) return 1;
    }

    // main diag
    {
        int ok = 1;
        for (int i = 0; i < BOARD_N; i++) {
            if (b[i][i] != sym) { ok = 0; break; }
        }
        if (ok) return 1;
    }

    // anti diag
    {
        int ok = 1;
        for (int i = 0; i < BOARD_N; i++) {
            if (b[i][BOARD_N - 1 - i] != sym) { ok = 0; break; }
        }
        if (ok) return 1;
    }

    return 0;
}

static int check_draw(char b[BOARD_N][BOARD_N]) {
    for (int r = 0; r < BOARD_N; r++) {
        for (int c = 0; c < BOARD_N; c++) {
            if (b[r][c] == EMPTY_CELL) return 0;
        }
    }
    return 1;
}

/* ---------- main handler ---------- */

void handle_client(int client_socket, int player_id, int human_player_number) {
    printf("Player %d connected (ID: %d).\n", human_player_number, player_id);

    // store socket for scheduler broadcast
    pthread_mutex_lock(&gameData->board_mutex);
    gameData->client_sockets[player_id] = client_socket;
    pthread_mutex_unlock(&gameData->board_mutex);

    char logBuf[128];
    snprintf(logBuf, sizeof(logBuf), "Player %d connected.", human_player_number);
    log_message(logBuf);

    // Ask name
    send_str(client_socket, "Enter your name: ");
    char buf[BUFFER_SIZE];

    memset(buf, 0, sizeof(buf));
    int n = (int)recv(client_socket, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client_socket);
        exit(0);
    }
    buf[n] = '\0';
    trim_newline(buf);

    pthread_mutex_lock(&gameData->board_mutex);
    snprintf(gameData->player_name[player_id],
             sizeof(gameData->player_name[player_id]),
             "%.31s", buf); // limit to 31 chars
    pthread_mutex_unlock(&gameData->board_mutex);

    // Ask symbol
    while (1) {
        send_str(client_socket, "Choose your symbol (X/Y/Z): ");

        memset(buf, 0, sizeof(buf));
        n = (int)recv(client_socket, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[player_id] = false;
            gameData->client_sockets[player_id] = -1;
            pthread_mutex_unlock(&gameData->board_mutex);
            close(client_socket);
            exit(0);
        }
        buf[n] = '\0';
        trim_newline(buf);

        char sym = 0;
        if (buf[0] == 'X' || buf[0] == 'x') sym = 'X';
        else if (buf[0] == 'Y' || buf[0] == 'y') sym = 'Y';
        else if (buf[0] == 'Z' || buf[0] == 'z') sym = 'Z';

        if (!sym) {
            send_str(client_socket, "Invalid symbol. Please choose X, Y, or Z.\n");
            continue;
        }

        pthread_mutex_lock(&gameData->board_mutex);
        int taken = symbol_taken(sym);
        if (!taken) {
            gameData->player_symbol[player_id] = sym;
        }
        pthread_mutex_unlock(&gameData->board_mutex);

        if (taken) {
            send_str(client_socket, "That symbol is already taken. Choose another.\n");
            continue;
        }

        char okmsg[80];
        snprintf(okmsg, sizeof(okmsg), "Your symbol has been assigned: %c\n", sym);
        send_str(client_socket, okmsg);

        snprintf(logBuf, sizeof(logBuf), "Player %d chose symbol %c", human_player_number, sym);
        log_message(logBuf);

        break;
    }

    // Wait message; scheduler will broadcast the big board screens
    send_str(client_socket, "Waiting for game to start...\n");

    // Main loop: receive grid moves
    while (1) {
        memset(buf, 0, sizeof(buf));
        int bytes = (int)recv(client_socket, buf, sizeof(buf) - 1, 0);

        if (bytes <= 0) {
            printf("Player %d disconnected.\n", human_player_number);

            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[player_id] = false;
            gameData->client_sockets[player_id] = -1;
            gameData->player_count--;
            pthread_mutex_unlock(&gameData->board_mutex);

            snprintf(logBuf, sizeof(logBuf), "Player %d disconnected.", human_player_number);
            log_message(logBuf);
            break;
        }

        buf[bytes] = '\0';
        trim_newline(buf);

        pthread_mutex_lock(&gameData->board_mutex);

        // If round already ended, ignore moves
        if (gameData->round_over) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Round already ended. Please wait for reset...\n");
            continue;
        }

        // Must be your turn
        if (gameData->current_turn_id != player_id) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "It is not your turn. Please wait...\n");
            continue;
        }

        int r, c;
        if (!parse_grid_number(buf, &r, &c)) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Invalid input. Please enter a grid number.\n");
            send_prompt(client_socket, BOARD_N * BOARD_N);
            continue;
        }

        // Validate spot
        if (gameData->board[r][c] != EMPTY_CELL) {
            pthread_mutex_unlock(&gameData->board_mutex);
            send_str(client_socket, "Invalid move. Spot taken.\n");
            // ✅ THIS is where your bug was: it must NOT say 1-9.
            send_prompt(client_socket, BOARD_N * BOARD_N);
            continue;
        }

        // Place move
        char my_sym = gameData->player_symbol[player_id];
        gameData->board[r][c] = my_sym;

        snprintf(logBuf, sizeof(logBuf), "Player %d placed %c at (%d,%d)", human_player_number, my_sym, r, c);
        log_message(logBuf);

        // Win / Draw
        if (check_win(gameData->board, my_sym)) {

            strcpy(gameData->scores[player_id].name, gameData->player_name[player_id]);
            gameData->scores[player_id].wins++;

            save_scores(); // udpate the scores.txt

            gameData->round_over = true;
            gameData->turn_complete = true; // lets scheduler broadcast update
            pthread_mutex_unlock(&gameData->board_mutex);

            send_str(client_socket, "You won this round!\n");
            continue;
        }

        if (check_draw(gameData->board)) {
            gameData->draw = true;
            gameData->round_over = true;
            gameData->turn_complete = true;
            pthread_mutex_unlock(&gameData->board_mutex);

            send_str(client_socket, "Draw! No empty spots left.\n");
            continue;
        }

        // Normal continue
        gameData->turn_complete = true;
        pthread_mutex_unlock(&gameData->board_mutex);

        send_str(client_socket, "Move accepted.\n");
        // scheduler will show next board + whose turn
    }

    close(client_socket);
    exit(0);
}
