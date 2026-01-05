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


/* Shared memory constants */
struct Game
{
    char boardGame[4][4]; //board game 4x4
    int currentPlayer;
    bool game_active;   
    pthread_mutex_t board_mutex; //mutex for synchronizing access to the game board
    
    int player_sockets[2]; //sockets for two players
    int player_count; //number of connected players
};

struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);
const int SERVER_PORT = 8080;
char server_message[1024];
int PLAYER_IDS[2] = {0, 1};



/* Thread functions */  
void* scheduler_thread (void* arg)
{
    // scheduler logic here 
    // player turns, check win conditions, update game state
    return NULL;
}

void* logger_thread (void* arg)
{ 
    // record log game events, player actions, errors into a file game.log
    return NULL;
}



void* handle_client (void* arg)
{
    // Handle client logic here 
    int my_id = *(int*)arg;
    printf ("Player %d connected. Current Player: %d (socket=%d)\n", my_id + 1, gameData->currentPlayer, gameData->player_sockets[my_id]);
    
    return NULL;
}


int main ()
{
    shm_unlink (SHM_NAME); //remove existing shared memory segment if any

    // player joined the game
    int playerNumber = 0;
    

    //create and open shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); 
    if (shm_fd == -1) {
        perror("shm_open failed");
        pthread_exit(NULL);
    }
    
    //size of shared memory
    ftruncate (shm_fd, SHM_SIZE);

    //map shared memory 
    gameData = (struct Game*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); 
    if (gameData == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    //data initialization
    gameData->player_count = 0;
    gameData->game_active = true;
    gameData->currentPlayer = 1;

    //initialize Mutex 
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameData->board_mutex, &attr);


    pthread_t scheduler, logger;
    pthread_create(&scheduler, NULL, scheduler_thread, NULL);
    pthread_create(&logger, NULL, logger_thread, NULL);
    

    //create Server Socket (listen)
    int server_fd;
    server_fd = socket (AF_INET, SOCK_STREAM, 0);
    //define the server and client address 
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons (SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    //bind the socket to the specified IP and port
    if(bind (server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror ("Binding failed. Exiting.\n");
        exit (1);
    }

    listen (server_fd, 5);

    pthread_t client_thread[2];
    while (playerNumber < 2)
    {
        printf ("Waiting for player %d to connect...\n", playerNumber + 1);

        //accept client connections (0 = success, -1 = failure)
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int new_client_fd = accept (server_fd, (struct sockaddr *) &client_address, &client_len);
        
        if (new_client_fd == -1)
        {
            perror ("Failed to accept client connection. Exiting.\n");
            exit (1);
        }
        
        gameData->player_sockets[playerNumber] = new_client_fd;
        gameData->player_count++;

        pthread_create(&client_thread[playerNumber], NULL, handle_client, (void*)&PLAYER_IDS[playerNumber]);

        printf ("Client %d connected successfully.\n", gameData->player_count);
        
        playerNumber++;
        
    }

    //send data to client
    //strcpy(server_message, "Welcome to the Game Server!");
    //send(new_client_fd, server_message, sizeof(server_message), 0);


    
    //wait for client threads to finish
    pthread_join(scheduler, NULL);
    pthread_join(logger, NULL);

    pthread_join(client_thread[0], NULL);
    pthread_join(client_thread[1], NULL);

    //cleanup
    close (server_fd);
    munmap(gameData, SHM_SIZE);
    shm_unlink(SHM_NAME);
    

    return 0;
}