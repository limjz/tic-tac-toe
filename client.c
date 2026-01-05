#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>


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
char server_response[1024];


int main()
{
    // create socket 
    int server_socket;
    server_socket = socket (AF_INET, SOCK_STREAM, 0);

    //specify an address for the socket 
    struct sockaddr_in server_address; 
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons (SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    // connect to server (0 = success, -1 = failure)
    int connection_status = connect (server_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    if(connection_status == -1)
    {
        printf ("Connection to server failed. Exiting.\n");
        exit (1);
    }
    else 
    {
        printf ("Successfully connected to server on port %d\n", SERVER_PORT);
    }

    //receive data from server
    recv (server_socket, server_response, sizeof(server_response), 0);

    //print server response 
    printf ("Server Response: %s\n", server_response);

    //close socket
    close (server_socket);

    return 0;
}