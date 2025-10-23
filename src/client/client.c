#include <stdio.h>
#include <stdlib.h>
#include <libc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "client.h"

#include "../common/api.h"
#include "../common/model.h"

#define PORT 8080

int start_client() {
    int client_fd;
    struct sockaddr_in serv_addr;

    // Create client socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(client_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Send CONNECT call type
        CallType call_type = CONNECT;
        if (send(client_fd, &call_type, sizeof(CallType), 0) <= 0) {
            perror("send failed");
            exit(EXIT_FAILURE);
        }

        // Send username
        char username[USERNAME_SIZE + 1] = "test_user";
        if (send(client_fd, username, USERNAME_SIZE + 1, 0) <= 0) {
            perror("send failed");
            exit(EXIT_FAILURE);
        }

        // Receive User struct
        uint8_t buffer[1024];
        if (recv(client_fd, buffer, 1024, 0) <= 0) {
            perror("recv failed");
            exit(EXIT_FAILURE);
        }

        // Deserialize User struct
        User user;
        deserialize_User(buffer, &user);

        // Print received User struct
        printf("Received User struct: username = %s, id = %d, bio = %s, total_score = %d, total_games = %d, total_wins = %d\n",
               user.username, user.id, user.bio, user.total_score, user.total_games, user.total_wins);

        // Free allocated memory
        free(user.bio);

        sleep(2);
    }

    return 0;
}
