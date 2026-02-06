#include "game.h"

struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);

void signal_handler(int signo) {
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

static void init_game_locked(void) {

    load_scores();

    // Init board to EMPTY_CELL
    for (int r = 0; r < BOARD_N; r++) {
        for (int c = 0; c < BOARD_N; c++) {
            gameData->board[r][c] = EMPTY_CELL;
        }

        gameData->draw = false;
        gameData->round_over = false;
    }

    // Game state
    gameData->game_active = true;
    gameData->turn_complete = false;
    gameData->current_turn_id = -1;

    // ✅ required by your request + client_handler usage
    gameData->draw = false;

    // ✅ required for logger.c
    gameData->log_head = 0;
    gameData->log_tail = 0;

    // Players
    gameData->player_count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        gameData->player_active[i] = false;
        gameData->client_sockets[i] = -1;
        gameData->player_symbol[i] = 0;
        gameData->player_name[i][0] = '\0';
    }

    // Clear log queue (optional but clean)
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        gameData->log_queue[i][0] = '\0';
    }
}

void shutdown_handler(int signo) {
    printf("\nShutting down...\n");
    
    if (gameData) {
        gameData->game_active = false; // Kill threads
        save_scores();                 // SAVE SCORES (Requirement 7.1)
    }
    
    // Clean up
    shm_unlink(SHM_NAME);
    exit(0);
}

int main() {

    signal(SIGCHLD, signal_handler);
    signal(SIGINT, shutdown_handler);

    shm_unlink(SHM_NAME);

    // Shared memory create
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("shm_open"); exit(1); }

    if (ftruncate(shm_fd, SHM_SIZE) < 0) { perror("ftruncate"); exit(1); }

    // Map shared memory
    gameData = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (gameData == MAP_FAILED) { perror("mmap"); exit(1); }

    // Init mutexes (process-shared)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&gameData->board_mutex, &attr);
    pthread_mutex_init(&gameData->log_mutex, &attr);

    // Init game data
    pthread_mutex_lock(&gameData->board_mutex);
    init_game_locked();
    pthread_mutex_unlock(&gameData->board_mutex);

    // Start scheduler + logger threads
    pthread_t scheduler, logger;
    pthread_create(&scheduler, NULL, scheduler_thread, NULL);
    pthread_create(&logger, NULL, logger_thread, NULL);

    // Socket setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    signal(SIGCHLD, signal_handler);

    printf("Server started. Waiting for players...\n");

    // Accept loop
    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int client_fd = accept(server_fd, (struct sockaddr*)&caddr, &clen);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        pthread_mutex_lock(&gameData->board_mutex);

        int id = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!gameData->player_active[i]) {
                id = i;
                gameData->player_active[i] = true;
                gameData->client_sockets[i] = client_fd; // parent keeps for broadcast
                gameData->player_count++;
                break;
            }
        }

        pthread_mutex_unlock(&gameData->board_mutex);

        if (id == -1) {
            send(client_fd, "Server full.\n", strlen("Server full.\n"), 0);
            close(client_fd);
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child handles this client
            close(server_fd);
            handle_client(client_fd, id, id + 1);
            exit(0);
        } else if (pid > 0) {
            // Parent keeps socket open for broadcast
            printf("Client connected (ID: %d, PID: %d)\n", id + 1, pid);
        } else {
            // fork failed
            perror("fork");
            close(client_fd);

            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[id] = false;
            gameData->client_sockets[id] = -1;
            gameData->player_count--;
            pthread_mutex_unlock(&gameData->board_mutex);
        }
    }

    // (Not reached normally)
    close(server_fd);
    return 0;
}

