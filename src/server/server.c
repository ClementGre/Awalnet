#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include "../common/api.h"

// Define constants
#define PORT 8080
#define MAX_CLIENTS 10
#define USERNAME_SIZE 32


int start_server() {
    printf("🚀 Starting Awalnet server...\n");
    int server_fd, client_fds[MAX_CLIENTS];
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize client_fds
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = 0;
    }

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;
    struct timeval timeout;
    timeout.tv_sec = 1; // 1-second timeout
    timeout.tv_usec = 0;

    printf("✨ Server listening on port %d\n", PORT);
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] > 0) {
                FD_SET(client_fds[i], &read_fds);
            }
            if (client_fds[i] > max_fd) {
                max_fd = client_fds[i];
            }
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("select failed");
            continue;
        } else if (activity == 0) {
            // Timeout occurred, continue to next iteration
            continue;
        }

        // Handle new connections
        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket;
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept failed");
                continue;
            }
            // Add new socket to client_fds
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_fds[i] == 0) {
                    client_fds[i] = new_socket;
                    break;
                }
            }
        }

        // Handle data from clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] > 0 && FD_ISSET(client_fds[i], &read_fds)) {
                CallType call_type;
                if (read(client_fds[i], &call_type, sizeof(CallType)) <= 0) {
                    // Handle read error or client disconnection
                    close(client_fds[i]);
                    client_fds[i] = 0;
                    continue;
                }

                switch (call_type) {
                    case CONNECT: {
                        printf("Received CONNECT command from client %d\n", i);
                        char username[USERNAME_SIZE + 1];
                        if (read(client_fds[i], username, USERNAME_SIZE + 1) <= 0) {
                            // Handle read error or client disconnection
                            close(client_fds[i]);
                            client_fds[i] = 0;
                            continue;
                        }
                        // Process CONNECT command
                        User user = newUser(username, "");
                        uint8_t user_buffer[1024]; // Ensure buffer is large enough
                        serialize_User(&user, user_buffer);
                        send(client_fds[i], user_buffer, sizeof(user_buffer), 0);
                        break;
                    }
                    case CHALLENGE: {
                        printf("Received CHALLENGE command from client %d\n", i);
                        int opponent_user_id;
                        if (read(client_fds[i], &opponent_user_id, sizeof(int)) <= 0) {
                            // Handle read error or client disconnection
                            close(client_fds[i]);
                            client_fds[i] = 0;
                            continue;
                        }
                        // Process CHALLENGE command
                        break;
                    }
                    default:
                        // Handle unknown CallType
                        break;
                }
            }
        }
    }

    return 0;
}
