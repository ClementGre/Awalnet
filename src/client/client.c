#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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


    char username[USERNAME_SIZE + 1];
    printf("Enter username: ");
    fgets(username, USERNAME_SIZE + 1, stdin);
    username[strcspn(username, "\n")] = 0; // Remove newline character

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

    int connect_call_type_send = 0;
    User user;

    while (1) {

        if (!connect_call_type_send) {
            // Send CONNECT call type
            CallType call_type = CONNECT;
            if (send(client_fd, &call_type, sizeof(CallType), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            connect_call_type_send = 1;

            // Send username
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
            deserialize_User(buffer, &user);

            // Print received User struct
            printf("Received User struct: username = %s, id = %d, bio = %s, total_score = %d, total_games = %d, total_wins = %d\n",
                   user.username, user.id, user.bio, user.total_score, user.total_games, user.total_wins);

        }

        // set of actions to perform
        printf("\n===== MENU =====\n");
        printf(" 1 - View your profile\n");
        printf(" 2 - Display online users\n");
        printf(" 3 - Challenge a user\n");
        printf(" 4 - Exit\n");

        int choice;
        printf("Enter your choice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number between 1 and 4.\n");
            while (getchar() != '\n');
            continue;
        }

        while (getchar() != '\n');

        switch (choice) {
            case 1:
                // view profile
                printUser(&user);
                break;
            case 2:
                CallType call_type = LIST_USERS;
                if (send(client_fd, &call_type, sizeof(CallType), 0) <= 0) {
                    perror("send failed");
                    exit(EXIT_FAILURE);
                }

                char user_list_buffer[1024] = {0};
                if (recv(client_fd, user_list_buffer, sizeof(user_list_buffer), 0) <= 0) {
                    perror("recv failed");
                    exit(EXIT_FAILURE);
                }

                printf("Online users:\n%s", user_list_buffer);

                break;
            case 3:
                printf("Challenge feature not implemented yet.\n");
                break;
            case 4:
                close(client_fd);
                printf("Disconnected from server.\n");
                exit(EXIT_SUCCESS);
            default:
                printf("Invalid choice. Please enter a number between 1 and 4.\n");
                break;
        }

        sleep(1);
    }

    return 0;
}
