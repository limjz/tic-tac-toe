#include "game.h"

// GLOBAL DEFINITIONS (owned here)
struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);

void signal_handler(int signo) {
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
}

int main() {
    shm_unlink(SHM_NAME);

    // Create shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open failed"); exit(1); }
    if (ftruncate(shm_fd, SHM_SIZE) == -1) { perror("ftruncate failed"); exit(1); }

    // Map shared memory
    gameData = (struct Game*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (gameData == MAP_FAILED) { perror("mmap failed"); exit(1); }

    // Init game data
    memset(gameData, 0, SHM_SIZE);
    gameData->player_count = 0;
    gameData->game_active = true;
    gameData->currentPlayer = 1;
    gameData->turn_complete = false;
    gameData->winner = 0;
    gameData->draw = false;
    gameData->log_head = 0;
    gameData->log_tail = 0;

    for (int i = 0; i < 5; i++) {
        gameData->player_active[i] = false;
        gameData->client_sockets[i] = -1;
    }

    // Init mutexes (process-shared)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameData->board_mutex, &attr);
    pthread_mutex_init(&gameData->log_mutex, &attr);

    // Start threads
    pthread_t scheduler, logger;
    pthread_create(&scheduler, NULL, scheduler_thread, NULL);
    pthread_create(&logger, NULL, logger_thread, NULL);

    // Socket setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Binding failed");
        exit(1);
    }
    if (listen(server_fd, 5) == -1) {
        perror("listen failed");
        exit(1);
    }

    signal(SIGCHLD, signal_handler);

    printf("Server started. Waiting for players...\n");

    // Accept clients
    while (gameData->game_active) {
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int new_client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);

        if (new_client_fd == -1) {
            if (errno == EINTR) continue;
            if (!gameData->game_active) break;
            continue;
        }

        pthread_mutex_lock(&gameData->board_mutex);

        int assigned_id = -1;
        for (int i = 0; i < 5; i++) {
            if (!gameData->player_active[i]) {
                assigned_id = i;
                gameData->player_active[i] = true;
                gameData->client_sockets[i] = new_client_fd; // keep for broadcast in scheduler
                gameData->player_count++;
                break;
            }
        }

        pthread_mutex_unlock(&gameData->board_mutex);

        if (assigned_id == -1) {
            printf("Server full! Rejecting client.\n");
            close(new_client_fd);
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child handles this one client
            close(server_fd);
            handle_client(new_client_fd, assigned_id, assigned_id + 1);
            exit(0);
        } else if (pid > 0) {
            // Parent keeps socket open for broadcast; do NOT close(new_client_fd)
            printf("Client connected (ID: %d, PID: %d)\n", assigned_id + 1, pid);
        } else {
            perror("fork failed");
            close(new_client_fd);

            pthread_mutex_lock(&gameData->board_mutex);
            gameData->player_active[assigned_id] = false;
            gameData->client_sockets[assigned_id] = -1;
            gameData->player_count--;
            pthread_mutex_unlock(&gameData->board_mutex);
        }
    }

    // Shutdown
    gameData->game_active = false;

    pthread_join(scheduler, NULL);
    pthread_join(logger, NULL);

    // Close stored sockets
    pthread_mutex_lock(&gameData->board_mutex);
    for (int i = 0; i < 5; i++) {
        if (gameData->client_sockets[i] >= 0) {
            close(gameData->client_sockets[i]);
            gameData->client_sockets[i] = -1;
        }
    }
    pthread_mutex_unlock(&gameData->board_mutex);

    close(server_fd);
    munmap(gameData, SHM_SIZE);
    shm_unlink(SHM_NAME);
    return 0;
}
