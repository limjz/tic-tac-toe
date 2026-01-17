#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 8080

// Shared memory game state (we use boardGame[0..2][0..2], rest unused)
struct Game {
    char boardGame[4][4];            // we will use 3x3 portion for client (0..2)
    int currentPlayer;               // 0,1,2 for X,O,Y
    bool game_active;
    pthread_mutex_t board_mutex;     // process-shared mutex
    int player_count;                // number of connected players
    char player_name[3][64];         // store JOIN names
};

static struct Game *gameData = NULL;
static const char *SHM_NAME = "/game_shm";
static const size_t SHM_SIZE = sizeof(struct Game);

static char symbol_for_player(int pid) {
    if (pid == 0) return 'X';
    if (pid == 1) return 'O';
    return 'Y';
}

// Build a 9-char board string for the client: "........."
static void build_board9(char out9[10]) {
    int k = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            char ch = gameData->boardGame[r][c];
            if (ch == '\0') ch = '.';
            out9[k++] = ch;
        }
    }
    out9[9] = '\0';
}

// Initialize board to '.'
static void init_board(void) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            gameData->boardGame[r][c] = '.';
}

static void safe_send_line(int sock, const char *line) {
    // line must include '\n' at end
    ssize_t n = send(sock, line, strlen(line), 0);
    (void)n;
}

static void send_board(int sock) {
    char b9[10];
    build_board9(b9);

    char msg[64];
    snprintf(msg, sizeof(msg), "BOARD %s\n", b9);
    safe_send_line(sock, msg);
}

// ---------- Thread functions ----------
static void *scheduler_thread(void *arg) {
    (void)arg;
    printf("Scheduler start working: Monitoring game state...\n");
    fflush(stdout);

    while (gameData && gameData->game_active) {
        usleep(3000000); // 3s
    }

    printf("Scheduler: Game finished. Exiting.\n");
    fflush(stdout);
    return NULL;
}

static void *logger_thread(void *arg) {
    (void)arg;
    // You can implement logging to game.log later
    while (gameData && gameData->game_active) {
        usleep(5000000);
    }
    return NULL;
}

// ---------- Signal handler (avoid zombies) ----------
static void signal_handler(int signo) {
    (void)signo;
    // reap children
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // keep reaping
    }
}

// ---------- Client handling ----------
static void handle_client(int client_socket, int player_id) {
    // Child process handles one client
    char sym = symbol_for_player(player_id);

    // Send initial protocol lines so the client displays board immediately
    safe_send_line(client_socket, "MESSAGE Welcome to the server !!\n");

    char symline[32];
    snprintf(symline, sizeof(symline), "SYMBOL %c\n", sym);
    safe_send_line(client_socket, symline);

    // Send initial board and first turn hint
    pthread_mutex_lock(&gameData->board_mutex);
    send_board(client_socket);

    if (gameData->currentPlayer == player_id) {
        safe_send_line(client_socket, "YOUR_TURN\n");
    } else {
        safe_send_line(client_socket, "MESSAGE Waiting for your turn...\n");
    }
    pthread_mutex_unlock(&gameData->board_mutex);

    // Main receive loop
    char buf[BUFFER_SIZE];
    while (1) {
        memset(buf, 0, sizeof(buf));
        int bytes = (int)recv(client_socket, buf, sizeof(buf) - 1, 0);
        if (bytes <= 0) {
            printf("Player %d disconnected.\n", player_id + 1);
            fflush(stdout);
            break;
        }

        // Trim CRLF
        for (int i = 0; i < bytes; i++) {
            if (buf[i] == '\r' || buf[i] == '\n') {
                buf[i] = '\0';
                break;
            }
        }

        printf("Player %d sent: %s\n", player_id + 1, buf);
        fflush(stdout);

        // Handle JOIN
        if (strncmp(buf, "JOIN ", 5) == 0) {
            const char *name = buf + 5;

            pthread_mutex_lock(&gameData->board_mutex);
            snprintf(gameData->player_name[player_id], sizeof(gameData->player_name[player_id]), "%s", name);
            pthread_mutex_unlock(&gameData->board_mutex);

            char msg[128];
            snprintf(msg, sizeof(msg), "MESSAGE Hello %s, you are %c\n", name, sym);
            safe_send_line(client_socket, msg);

            // resend board/turn hint
            pthread_mutex_lock(&gameData->board_mutex);
            send_board(client_socket);
            if (gameData->currentPlayer == player_id) safe_send_line(client_socket, "YOUR_TURN\n");
            pthread_mutex_unlock(&gameData->board_mutex);
            continue;
        }

        // Handle MOVE r c
        if (strncmp(buf, "MOVE ", 5) == 0) {
            int r = -1, c = -1;
            if (sscanf(buf + 5, "%d %d", &r, &c) != 2) {
                safe_send_line(client_socket, "INVALID Bad MOVE format\n");
                continue;
            }
            if (r < 0 || r > 2 || c < 0 || c > 2) {
                safe_send_line(client_socket, "INVALID Out of range (0..2)\n");
                continue;
            }

            pthread_mutex_lock(&gameData->board_mutex);

            // turn check
            if (gameData->currentPlayer != player_id) {
                pthread_mutex_unlock(&gameData->board_mutex);
                safe_send_line(client_socket, "INVALID Not your turn\n");
                continue;
            }

            // cell check
            if (gameData->boardGame[r][c] != '.') {
                pthread_mutex_unlock(&gameData->board_mutex);
                safe_send_line(client_socket, "INVALID Cell already taken\n");
                continue;
            }

            // place symbol
            gameData->boardGame[r][c] = sym;

            // advance turn
            gameData->currentPlayer = (gameData->currentPlayer + 1) % 3;

            // send updated board back to this client
            send_board(client_socket);

            // tell this client to wait now
            safe_send_line(client_socket, "MESSAGE Move accepted. Waiting...\n");

            pthread_mutex_unlock(&gameData->board_mutex);
            continue;
        }

        // Unknown command
        safe_send_line(client_socket, "MESSAGE Unknown command\n");
    }

    close(client_socket);
    exit(0);
}

int main(int argc, char *argv[]) {
    // Choose port
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }
    printf("Server will listen on port %d\n", port);
    fflush(stdout);

    // reset shared memory
    shm_unlink(SHM_NAME);

    // create shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return 1;
    }
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate failed");
        return 1;
    }

    gameData = (struct Game *)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (gameData == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    // init shared data
    gameData->game_active = true;
    gameData->player_count = 0;
    gameData->currentPlayer = 0;
    init_board();
    for (int i = 0; i < 3; i++) gameData->player_name[i][0] = '\0';

    // init mutex for processes
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameData->board_mutex, &attr);

    // start threads
    pthread_t scheduler, logger;
    pthread_create(&scheduler, NULL, scheduler_thread, NULL);
    pthread_create(&logger, NULL, logger_thread, NULL);

    // create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        return 1;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons((uint16_t)port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("bind failed");
        return 1;
    }

    if (listen(server_fd, 5) == -1) {
        perror("listen failed");
        return 1;
    }

    // handle child exits
    signal(SIGCHLD, signal_handler);

    // accept up to 3 players (X,O,Y)
    while (gameData->player_count < 3) {
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            perror("accept failed");
            break;
        }

        pthread_mutex_lock(&gameData->board_mutex);
        int player_id = gameData->player_count; // 0..2
        gameData->player_count++;
        pthread_mutex_unlock(&gameData->board_mutex);

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(client_fd);
            break;
        } else if (pid == 0) {
            // child
            close(server_fd);
            handle_client(client_fd, player_id);
        } else {
            // parent
            close(client_fd);
            printf("Client %d connected successfully.\n", player_id + 1);
            fflush(stdout);
        }
    }

    // cleanup (server stop)
    gameData->game_active = false;

    pthread_join(scheduler, NULL);
    pthread_join(logger, NULL);

    close(server_fd);
    munmap(gameData, SHM_SIZE);
    shm_unlink(SHM_NAME);

    return 0;
}
