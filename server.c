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

#define BUFFER_SIZE 1024
#define SERVER_PORT 8080


/* Shared memory constants */
struct Game
{
    char boardGame[4][4]; //board game 4x4
    int currentPlayer;
    bool game_active;   
    pthread_mutex_t board_mutex; //mutex for synchronizing access to the game board
    int player_count; //number of connected players
};

struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);
char server_message[BUFFER_SIZE];
int PLAYER_IDS[3] = {0, 1, 2};



/* Thread functions */  
void* scheduler_thread (void* arg)
{
    /*
    scheduler logic here 
    player turns, check win conditions, update game state
    */

    printf("Scheduler start working: Monitoring game state... \n");

    while (gameData ->game_active)  
    {
        usleep (10000000); // keep looping to keep the server side alive
    }

    printf ("Scheduler: Game finished !! Exiting Scheduler_Thread. \n");

    return NULL;
}

void* logger_thread (void* arg)
{ 
    // record log game events, player actions, errors into a file game.log
    return NULL;
}



void handle_client (int client_socket, int player_id, int player_count)
{
    // Handle client logic here 
    printf ("Player %d connected. Current Player: %d\n", player_id + 1, player_count);
    
    char msg_from_client[BUFFER_SIZE]; // message send from client
    char *msg = "Welcome to the server !!"; 
    send (client_socket, msg, strlen(msg), 0);
 
    while (1)
    {
        memset (msg_from_client, 0, BUFFER_SIZE); //refresh the buffer container 
        int bytes = recv (client_socket, msg_from_client, BUFFER_SIZE, 0); 

        if (bytes <= 0)
        {
            printf("Player %d disconnected. \n", player_id + 1);

            break;
        }


        printf ("Player %d send %s\n", player_id + 1, msg_from_client);
    }
    close(client_socket);
    exit(0);

}

void signal_handler (int signo)
{
    //killing the child process, prevent zombie
    while( waitpid (-1, NULL, WNOHANG) > 0);
    {
        //if the child is dead, the parents still ongoing, close the game-> game_active = false
        if (gameData != NULL && gameData->game_active) 
        {
            gameData->game_active = false; 
            printf ("Player disconnected. Game quit \n");

            kill(0, SIGTERM);
            exit(0);
        }
        
    }

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

    //map shared memory // init gameData
    gameData = (struct Game*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); 
    if (gameData == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    //data initialization
    gameData->player_count = 0;
    gameData->game_active = true;
    gameData->currentPlayer = 1;

    //Mutex initialization -> for fork() and restricted one player only can access the game board at once
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

    //define the server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons (SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;


    //avoid address already in use error
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror ("setsockopt failed. Exiting.\n");
        exit (1);
    }

    //bind the socket to the specified IP and port
    if(bind (server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror ("Binding failed. Exiting.\n");
        exit (1);
    }

    listen (server_fd, 5); //maximum server can handle 5 client

    
    signal(SIGCHLD, signal_handler); //check if the player still playing onot, if not end the server also

    while (playerNumber < 3)
    {
        //accept client connections (0 = success, -1 = failure)
        //define the client address
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int new_client_fd = accept (server_fd, (struct sockaddr *) &client_address, &client_len);
        
        if (new_client_fd == -1)
        {
            if (gameData->game_active == false)
            {
                printf("Game stopped, player disconnected, server shutdown"); 
                kill (0, SIGTERM);
                exit(0);
            }


            perror ("Failed to accept client connection. Exiting.\n");
            exit (1);
        }
        
        //fork() a new process to handle each client
        pid_t pid = fork();
        if (pid < 0)
        {
            perror ("Fork failed. Exiting.\n");
            exit (1);
        }

        else if (pid == 0) 
        {
            // ============= CHILD PROCESS ===========================

            close (server_fd); //close server socket in child process // child only need use client server
            handle_client (new_client_fd, PLAYER_IDS[playerNumber], playerNumber + 1);

            exit (0);
        }
        else 
        {
            // ============= PARENT PROCESS =========================

            close (new_client_fd); //close client socket in parent process // parents are the server no need kacau client
        }
        
        printf ("Client %d connected successfully.\n", playerNumber + 1);
        playerNumber++;
        
    }

    
    //wait for client threads to finish
    pthread_join(scheduler, NULL);
    pthread_join(logger, NULL);


    //cleanup
    close (server_fd);
    munmap(gameData, SHM_SIZE);
    shm_unlink(SHM_NAME);
    
     //if server shutdown, all process including the child end as well (final check)
    kill(0, SIGTERM);

    return 0;
}