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

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // get user username
    char username[USERNAME_SIZE + 1];
    printf("Enter username: ");
    fgets(username, USERNAME_SIZE + 1, stdin);
    username[strcspn(username, "\n")] = 0;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }

    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    // Handshake CONNECT
    CallType call_type = CONNECT;
    if (send(client_fd, &call_type, sizeof(CallType), 0) <= 0) {
        perror("send failed");
        exit(EXIT_FAILURE);
    }
    if (send(client_fd, username, USERNAME_SIZE + 1, 0) <= 0) {
        perror("send failed");
        exit(EXIT_FAILURE);
    }

    // Receive User info
    User user;
    uint8_t buffer[1024] = {0};
    if (recv(client_fd, buffer, sizeof(buffer), 0) <= 0) {
        perror("recv failed");
        exit(EXIT_FAILURE);
    }
    deserialize_User(buffer, &user);
    printf("Connecté en tant que %s (id=%d)\n", user.username, user.id);

    while (1) {
        // small delay to ensure we didn’t miss incoming messages
        sleep(1);

        // we have to listen for incoming challenges
        // this is done using select to not block the main thread (menu)
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);
        struct timeval tv = {0, 0};
        int n = select(client_fd + 1, &rfds, NULL, NULL, &tv);
        if (n > 0 && FD_ISSET(client_fd, &rfds)) {
            CallType incoming;
            if (recv(client_fd, &incoming, sizeof(incoming), 0) > 0) {
                if (incoming == CHALLENGE) {
                    int challenger_id = 0;
                    char challenger_username[USERNAME_SIZE + 1] = {0};
                    recv(client_fd, &challenger_id, sizeof(int), 0);
                    recv(client_fd, challenger_username, USERNAME_SIZE + 1, 0);
                    printf("\n>>> Défi reçu de %s (id=%d)\n", challenger_username, challenger_id);
                }
                if (incoming == ERROR){
                    char error_msg[256] = {0};
                    recv(client_fd, error_msg, sizeof(error_msg), 0);
                    printf(">>> Error : %s\n", error_msg);
                }
                if (incoming == SUCCESS){
                    printf(">>> Votre défi a été accepté !\n");
                }
            }
        }

        printf("\n===== MENU =====\n");
        printf(" 1 - Voir votre profil\n");
        printf(" 2 - Afficher les utilisateurs en ligne\n");
        printf(" 3 - Défier un utilisateur\n");
        printf(" 5 - Quitter\n");
        printf("Votre choix: ");

        int choice;
        if (scanf("%d", &choice) != 1) {
            printf("Entrée invalide.\n");
            while (getchar()!='\n');
            continue;
        }
        // we need to clear the input buffer to avoid issues with fgets later
        while (getchar()!='\n');

        if (choice == 1) {
            printUser(&user);
        } else if (choice == 2) {
            CallType ct = LIST_USERS;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            char user_list_buffer[1024] = {0};
            if (recv(client_fd, user_list_buffer, sizeof(user_list_buffer), 0) <= 0) {
                perror("recv failed");
                exit(EXIT_FAILURE);
            }
            printf("Utilisateurs en ligne:\n%s", user_list_buffer);
        } else if (choice == 3) {
            int opponent_id = 0;
            printf("Entrer l'id de l'adversaire: ");
            if (scanf("%d", &opponent_id) != 1) {
                printf("Entrée invalide.\n");
                while (getchar()!='\n');
                continue;
            }
            while (getchar()!='\n');
            CallType ct = CHALLENGE;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("send failed"); exit(EXIT_FAILURE);
            }
            if (send(client_fd, &opponent_id, sizeof(int), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
        } else if (choice == 5) {
            close(client_fd);
            printf("Déconnecté.\n");
            exit(EXIT_SUCCESS);
        } else {
            printf("Choix invalide.\n");
        }
    }
    return 0;
}