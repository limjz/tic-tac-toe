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
    pthread_mutex_t board_mutex; 
};

struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);
const int SERVER_PORT = 8080;
char server_message[1024];



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



void* handle_client (int player_id)
{
    // Handle client logic here 
    printf ("Player %d connected. Current Player: %d\n", player_id, gameData->currentPlayer);
    
}


int main ()
{
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
    
    pthread_t scheduler, logger;
    pthread_create(&scheduler, NULL, scheduler_thread, NULL);
    pthread_create(&logger, NULL, logger_thread, NULL);

    /*
    pthread_join(scheduler, NULL);
    pthread_join(logger, NULL);
    */

    //connect socket and listen for clients
    int server_socket, client_socket;
    server_socket = socket (AF_INET, SOCK_STREAM, 0);

    //define the server address 
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons (SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    //bind the socket to the specified IP and port
    bind (server_socket, (struct sockaddr *) &server_address, sizeof(server_address));

    listen (server_socket, 5);

    //accept client connections (0 = success, -1 = failure)
    client_socket = accept (server_socket, NULL, NULL);
    if (client_socket == -1)
    {
        printf ("Failed to accept client connection. Exiting.\n");
        exit (1);
    }
    else 
    {
        printf ("Client connected successfully.\n");
    }

    //send data to client
    strcpy(server_message, "Welcome to the Game Server!");
    send(client_socket, server_message, sizeof(server_message), 0);

    //close sockets
    close (client_socket);
    close (server_socket);


    //cleanup shared memory
    munmap(gameData, SHM_SIZE);
    shm_unlink(SHM_NAME);


    return 0;
}