#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 8080



/* Shared memory constants */
struct Game
{
    char boardGame[4][4]; //board game 4x4
    int currentPlayer;
    bool game_active;   
    pthread_mutex_t board_mutex; 
    int player_count; //number of connected players
};

struct Game *gameData;
const char* SHM_NAME = "/game_shm";
const size_t SHM_SIZE = sizeof(struct Game);
char server_response[BUFFER_SIZE];
int server_socket; // define the socket here so all can use


void* receive_handler (void * arg) // pthread to handle the respond or message from the server 
{
    char buffer[BUFFER_SIZE];

    while (1){
        memset(buffer, 0, sizeof(buffer)); //refresh the buffer container 
        int bytes = recv(server_socket, buffer, BUFFER_SIZE, 0);

        if (bytes <= 0)
        {
            printf ("Server Disconnected. GAME OVER \n"); 
            //break; // break the loop 

            exit (0);//terminate the program 
        }

        //printf ("\r%s\nServer send ", buffer);
        //fflush (stdout); 


    }
    return NULL;
}


int main()
{
    // create the predefine socket 
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
    
    printf ("Server Connected !\n"); 

    // create a pthread to handle the respond from the server// works bts
    pthread_t recv_thread; 
    pthread_create (&recv_thread, NULL, receive_handler, NULL); 
    
    char user_input [BUFFER_SIZE]; 
    while (1){ 

        // wait for user input
        if (fgets(user_input, BUFFER_SIZE, stdin) != NULL)
        {
            user_input [strcspn (user_input, "\n")] = 0; //get all the string input before enter key

            if (strcmp(user_input, "QUIT") == 0)
            {
                break;
            }

        }
    }





    //receive data from server
    //recv (server_socket, server_response, sizeof(server_response), 0);

    //print server response 
    //printf ("Server Response: %s\n", server_response);

    //close socket
    close (server_socket);

    return 0;
}