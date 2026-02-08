#include "game.h"
#include <sys/select.h>

int main(int argc, char *argv[]) {
    const char *server_ip = "127.0.0.1";
    if (argc >= 2) server_ip = argv[1];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Server Connected!\n");
    printf("Enter move as: row col (example: 1 2)\n\n");

    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // Receive message from server
        if (FD_ISSET(sock, &readfds)) {
            char buffer[BUFFER_SIZE];
            int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

            if (bytes <= 0) {
                printf("\nServer disconnected. GAME OVER.\n");
                break;
            }

            buffer[bytes] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        // Send user input to server
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[BUFFER_SIZE];
            if (!fgets(input, sizeof(input), stdin)) break;
            send(sock, input, strlen(input), 0);
        }
    }

    close(sock);
    return 0;
}
