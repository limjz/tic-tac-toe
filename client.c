
// client.c - Networked Tic-Tac-Toe Client (TCP)
// Features:
// - Displays board received from server
// - Handles user input when it's your turn (MOVE r c)
// - Minimal text protocol (line-based)
// - Local scores.txt load/save (simple "name wins" format)
//
// Build:
//   gcc -Wall -Wextra -O2 -o client client.c
//
// Run:
//   ./client <server_ip> <port> <player_name>
//
// NOTE: Assignment spec typically expects scoring persistence on the SERVER,
// but this client includes load/save because you requested it.

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define LINE_MAX_LEN 1024
#define NAME_MAX_LEN 64
#define SCORE_FILE   "scores.txt"

// ---------- Scores (local) ----------
typedef struct {
    char name[NAME_MAX_LEN];
    int wins;
} ScoreEntry;

typedef struct {
    ScoreEntry *entries;
    size_t len;
    size_t cap;
} ScoreTable;

static void scores_init(ScoreTable *t) {
    t->entries = NULL;
    t->len = 0;
    t->cap = 0;
}

static void scores_free(ScoreTable *t) {
    free(t->entries);
    t->entries = NULL;
    t->len = 0;
    t->cap = 0;
}

static void scores_reserve(ScoreTable *t, size_t need) {
    if (t->cap >= need) return;
    size_t newcap = (t->cap == 0) ? 8 : t->cap * 2;
    while (newcap < need) newcap *= 2;
    ScoreEntry *p = (ScoreEntry *)realloc(t->entries, newcap * sizeof(ScoreEntry));
    if (!p) {
        perror("realloc scores");
        exit(1);
    }
    t->entries = p;
    t->cap = newcap;
}

static int scores_find(ScoreTable *t, const char *name) {
    for (size_t i = 0; i < t->len; i++) {
        if (strcmp(t->entries[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void scores_set(ScoreTable *t, const char *name, int wins) {
    int idx = scores_find(t, name);
    if (idx >= 0) {
        t->entries[idx].wins = wins;
        return;
    }
    scores_reserve(t, t->len + 1);
    snprintf(t->entries[t->len].name, sizeof(t->entries[t->len].name), "%s", name);
    t->entries[t->len].wins = wins;
    t->len++;
}

static int scores_get(ScoreTable *t, const char *name) {
    int idx = scores_find(t, name);
    if (idx >= 0) return t->entries[idx].wins;
    return 0;
}

static void load_scores(ScoreTable *t) {
    FILE *f = fopen(SCORE_FILE, "r");
    if (!f) {
        // If file doesn't exist, that's fine. We'll create on save.
        return;
    }
    char name[NAME_MAX_LEN];
    int wins = 0;
    while (fscanf(f, "%63s %d", name, &wins) == 2) {
        scores_set(t, name, wins);
    }
    fclose(f);
}

static void save_scores(ScoreTable *t) {
    FILE *f = fopen(SCORE_FILE, "w");
    if (!f) {
        perror("fopen scores.txt");
        return;
    }
    for (size_t i = 0; i < t->len; i++) {
        fprintf(f, "%s %d\n", t->entries[i].name, t->entries[i].wins);
    }
    fclose(f);
}

// ---------- Board rendering ----------
static void draw_board_from_9(const char *cells9) {
    // cells9 is 9 chars: 'X','O','Y','.' etc.
    // We'll display '.' as blank.
    char c[9];
    for (int i = 0; i < 9; i++) {
        char ch = cells9[i];
        c[i] = (ch == '.' ? ' ' : ch);
    }

    printf("\n");
    printf("     0   1   2\n");
    printf("   +---+---+---+\n");
    printf(" 0 | %c | %c | %c |\n", c[0], c[1], c[2]);
    printf("   +---+---+---+\n");
    printf(" 1 | %c | %c | %c |\n", c[3], c[4], c[5]);
    printf("   +---+---+---+\n");
    printf(" 2 | %c | %c | %c |\n", c[6], c[7], c[8]);
    printf("   +---+---+---+\n");
    printf("\n");
}

static bool parse_board_line(const char *line, char out9[10]) {
    // Expect: "BOARD <9chars>"
    // Example: BOARD X.O..Y...
    const char *p = strstr(line, "BOARD ");
    if (!p) return false;
    p += 6;
    if ((int)strlen(p) < 9) return false;
    memcpy(out9, p, 9);
    out9[9] = '\0';
    return true;
}

// ---------- Socket helpers ----------
static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL, *rp = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv4/IPv6
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static ssize_t send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

// Read a full line ending with '\n' from socket into out (NUL-terminated).
// Returns:
//  >0 length (excluding '\0')
//   0 connection closed
//  -1 error
static int recv_line(int fd, char *out, int outcap) {
    static char buf[4096];
    static int buf_len = 0;

    while (1) {
        // Check if we already have a full line
        for (int i = 0; i < buf_len; i++) {
            if (buf[i] == '\n') {
                int line_len = i + 1;
                int copy_len = (line_len < outcap - 1) ? line_len : (outcap - 1);
                memcpy(out, buf, copy_len);
                out[copy_len] = '\0';

                // shift remaining
                memmove(buf, buf + line_len, buf_len - line_len);
                buf_len -= line_len;

                // trim CRLF
                size_t L = strlen(out);
                while (L > 0 && (out[L - 1] == '\n' || out[L - 1] == '\r')) {
                    out[L - 1] = '\0';
                    L--;
                }
                return (int)L;
            }
        }

        // Need more data
        if (buf_len >= (int)sizeof(buf)) {
            // buffer overflow protection
            fprintf(stderr, "recv_line internal buffer overflow\n");
            return -1;
        }

        ssize_t n = recv(fd, buf + buf_len, sizeof(buf) - (size_t)buf_len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0;
        buf_len += (int)n;
    }
}

// ---------- Input parsing ----------
static bool parse_move(const char *s, int *r, int *c) {
    // Accept formats: "1 2" or "1,2"
    int a = -1, b = -1;
    if (sscanf(s, "%d %d", &a, &b) == 2) {
        *r = a; *c = b;
        return true;
    }
    if (sscanf(s, "%d,%d", &a, &b) == 2) {
        *r = a; *c = b;
        return true;
    }
    return false;
}

static void print_help(void) {
    printf("Enter your move as: row col  (example: 1 2)\n");
    printf("Or: row,col       (example: 1,2)\n");
    printf("Rows/Cols are 0..2\n");
}

// ---------- Main ----------
int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <server_ip/host> <port> <player_name>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *port = argv[2];
    const char *player = argv[3];

    // Load local scores
    ScoreTable scores;
    scores_init(&scores);
    load_scores(&scores);
    int my_wins = scores_get(&scores, player);
    printf("Hello, %s. Local wins in %s: %d\n", player, SCORE_FILE, my_wins);

    int sock = connect_tcp(host, port);
    if (sock < 0) {
        fprintf(stderr, "Failed to connect to %s:%s\n", host, port);
        scores_free(&scores);
        return 1;
    }
    printf("Connected to server %s:%s\n", host, port);

    // Send JOIN
    char joinmsg[LINE_MAX_LEN];
    snprintf(joinmsg, sizeof(joinmsg), "JOIN %s\n", player);
    if (send_all(sock, joinmsg, strlen(joinmsg)) < 0) {
        perror("send JOIN");
        close(sock);
        scores_free(&scores);
        return 1;
    }

    bool your_turn = false;
    char my_symbol = '?';
    char board9[10] = ".........";

    print_help();

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        // Only read stdin when it's your turn (prevents typing at wrong time)
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // 1) Incoming server line
        if (FD_ISSET(sock, &rfds)) {
            char line[LINE_MAX_LEN];
            int n = recv_line(sock, line, sizeof(line));
            if (n == 0) {
                printf("Server closed connection.\n");
                break;
            }
            if (n < 0) {
                perror("recv_line");
                break;
            }

            // Handle known messages
            if (strncmp(line, "SYMBOL ", 7) == 0 && strlen(line) >= 8) {
                my_symbol = line[7];
                printf("You are symbol: %c\n", my_symbol);
            } else if (strncmp(line, "BOARD ", 6) == 0) {
                char tmp[10];
                if (parse_board_line(line, tmp)) {
                    memcpy(board9, tmp, 10);
                    draw_board_from_9(board9);
                } else {
                    printf("[Server] %s\n", line);
                }
            } else if (strcmp(line, "YOUR_TURN") == 0) {
                your_turn = true;
                printf("== YOUR TURN (%c) ==\n", my_symbol);
                printf("Move> ");
                fflush(stdout);
            } else if (strncmp(line, "INVALID", 7) == 0) {
                // server rejected your move
                printf("Server says: %s\n", line);
                your_turn = true;
                printf("Move> ");
                fflush(stdout);
            } else if (strncmp(line, "MESSAGE ", 8) == 0) {
                printf("%s\n", line + 8);
            } else if (strncmp(line, "WIN ", 4) == 0) {
                const char *winner = line + 4;
                printf("GAME OVER: Winner is %s\n", winner);

                // Update local score file if you won
                if (strcmp(winner, player) == 0) {
                    int w = scores_get(&scores, player);
                    scores_set(&scores, player, w + 1);
                    save_scores(&scores);
                    printf("Local score updated: %s wins = %d\n", player, w + 1);
                }
                break;
            } else if (strcmp(line, "DRAW") == 0) {
                printf("GAME OVER: Draw.\n");
                break;
            } else if (strcmp(line, "LOSE") == 0) {
                printf("GAME OVER: You lost.\n");
                break;
            } else {
                // unknown server line - still print it
                printf("[Server] %s\n", line);
            }
        }

        // 2) User input (only if it's your turn)
        if (your_turn && FD_ISSET(STDIN_FILENO, &rfds)) {
            char input[LINE_MAX_LEN];
            if (!fgets(input, sizeof(input), stdin)) {
                printf("stdin closed.\n");
                break;
            }
            // Trim newline
            size_t L = strlen(input);
            while (L > 0 && (input[L - 1] == '\n' || input[L - 1] == '\r')) {
                input[L - 1] = '\0';
                L--;
            }

            if (strcmp(input, "help") == 0) {
                print_help();
                printf("Move> ");
                fflush(stdout);
                continue;
            }

            int r = -1, c = -1;
            if (!parse_move(input, &r, &c) || r < 0 || r > 2 || c < 0 || c > 2) {
                printf("Invalid input. ");
                print_help();
                printf("Move> ");
                fflush(stdout);
                continue;
            }

            char movemsg[64];
            snprintf(movemsg, sizeof(movemsg), "MOVE %d %d\n", r, c);
            if (send_all(sock, movemsg, strlen(movemsg)) < 0) {
                perror("send MOVE");
                break;
            }

            // After sending, wait for server response (it decides validity/next turn)
            your_turn = false;
        }
    }

    close(sock);
    scores_free(&scores);
    return 0;
}
