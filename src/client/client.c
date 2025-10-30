#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "client.h"
#include "../common/api.h"
#include "../common/model.h"

#define PORT 8080

pthread_cond_t cond_challenge = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_user_profile = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_list_users = PTHREAD_COND_INITIALIZER;

// declaring mutex
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

User user;


void *listen_server(void *arg) {
    int client_fd = *(int *)arg;
    CallType incoming;

    while (1) {
        ssize_t n = recv(client_fd, &incoming, sizeof(incoming), 0);
        if (n <= 0) {
            printf("Connexion au serveur perdue.\n");
            exit(EXIT_FAILURE);
        }

        if (incoming == CHALLENGE) {
            pthread_mutex_lock(&lock);
            CallType ct = CHALLENGE_REQUEST_ANSWER;
            int challenger_id;
            char challenger_username[USERNAME_SIZE + 1] = {0};

            recv(client_fd, &challenger_id, sizeof(int), 0);
            recv(client_fd, challenger_username, USERNAME_SIZE + 1, 0);

            printf("Défi reçu de %s (id=%d)\n", challenger_username, challenger_id);

            int choix = -1;
            int input_valid = 0;

            // TODO : il faut que la lecture se fasse pour ça et pas pour le menu
            while (!input_valid) {
                printf("Accepter le défi ? (1 = Oui / 0 = Non) : ");
                fflush(stdout);

                if (scanf("%d", &choix) != 1) {
                    printf("Entrée invalide. Veuillez entrer 0 ou 1.\n");
                    while (getchar() != '\n');
                    continue;
                }
                while (getchar() != '\n');

                if (choix == 0 || choix == 1) {
                    input_valid = 1;
                } else {
                    printf("Veuillez entrer 0 (refuser) ou 1 (accepter).\n");
                }
            }

            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("calltype send failed");
                pthread_mutex_unlock(&lock);
                exit(EXIT_FAILURE);
            }
            if (send(client_fd, &challenger_id, sizeof(challenger_id), 0) <= 0) {
                perror("challenger_id send failed");
                pthread_mutex_unlock(&lock);
                exit(EXIT_FAILURE);
            }
            if (send(client_fd, &choix, sizeof(choix), 0) <= 0) {
                perror("choice send failed");
                pthread_mutex_unlock(&lock);
                exit(EXIT_FAILURE);
            }
            pthread_mutex_unlock(&lock);
        }

        else if (incoming == CHALLENGE_REQUEST_ANSWER){
            int challenged_user_id;
            int answer = -1;
            recv(client_fd, &challenged_user_id, sizeof(int), 0);
            recv(client_fd, &answer, sizeof (int), 0);
            if (answer == 0){
                printf("Your challenge proposal to %d has been refused\n", challenged_user_id);
            }
            else{
                printf("Your challenge proposal to %d has been accepted !!\n", challenged_user_id);
            }
            pthread_cond_signal(&cond_challenge);

        }
        else if (incoming == ERROR) {
            char error_msg[256] = {0};
            recv(client_fd, error_msg, sizeof(error_msg), 0);
            printf(">>> Erreur : %s\n", error_msg);
            pthread_cond_signal(&cond_challenge);
            pthread_cond_signal(&cond_user_profile);


        }
        else if (incoming == SUCCESS) {
            printf(">>> Votre demande de défi a été envoyé !\n");
            pthread_cond_signal(&cond_challenge);
        }
        else if (incoming == LIST_USERS) {
            char user_list_buffer[1024] = {0};
            recv(client_fd, user_list_buffer, sizeof(user_list_buffer), 0);
            printf("Utilisateurs en ligne :\n%s\n", user_list_buffer);
            pthread_cond_signal(&cond_list_users);

        }
        else if (incoming == CONSULT_USER_PROFILE){
            CallType ct = SENT_USER_PROFILE;
            int request_user_id;
            recv(client_fd, &request_user_id, sizeof(int), 0);
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("calltype send failed");
                exit(EXIT_FAILURE);
            }
            if (send(client_fd, &request_user_id, sizeof(int), 0) <= 0) {
                perror("request_user_id send failed");
                exit(EXIT_FAILURE);
            }
            uint8_t user_buffer[1024] = {0};
            serialize_User(&user, user_buffer);

            if (send(client_fd, &user_buffer, sizeof(user_buffer), 0) <= 0) {
                perror("user profile send failed");
                exit(EXIT_FAILURE);
            }

        }
        else if (incoming == RECEIVE_USER_PROFILE){
            uint8_t buffer[1024] = {0};
            if (recv(client_fd, buffer, sizeof(buffer), 0) <= 0) {
                perror("recv failed");
                exit(EXIT_FAILURE);
            }
            User user_received;
            deserialize_User(buffer, &user_received);
            printf("Profil de l'utilisateur demandé :\n");
            printUser(&user_received);
            pthread_cond_signal(&cond_user_profile);


        }
        else {
            printf(">>> Message inconnu reçu du serveur.\n");
        }


        fflush(stdout);
    }

    return NULL;
}

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
    uint8_t buffer[1024] = {0};
    if (recv(client_fd, buffer, sizeof(buffer), 0) <= 0) {
        perror("recv failed");
        exit(EXIT_FAILURE);
    }
    deserialize_User(buffer, &user);
    printf("Connecté en tant que %s (id=%d)\n", user.username, user.id);

    pthread_t listener_thread;
    pthread_create(&listener_thread, NULL, listen_server, &client_fd);

    while (1) {
        printf("\n===== MENU =====\n");
        printf(" 1 - Voir votre profil\n");
        printf(" 2 - Afficher les utilisateurs en ligne\n");
        printf(" 3 - Défier un utilisateur\n");
        printf(" 4 - Consulter le profil d'un utilisateur\n");
        printf(" 5 - Quitter\n");
        printf("Votre choix: ");

        int choice;
        if (scanf("%d", &choice) != 1) {
            printf("Entrée invalide.\n");
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n'); // vider buffer

        if (choice == 1) {
            printUser(&user);
        }
        else if (choice == 2) {
            CallType ct = LIST_USERS;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            pthread_cond_wait(&cond_list_users, &lock);
        }
        else if (choice == 3) {
            int opponent_id;
            printf("Entrer l'id de l'adversaire: ");
            if (scanf("%d", &opponent_id) != 1) {
                printf("Entrée invalide.\n");
                while (getchar() != '\n');
                continue;
            }
            while (getchar() != '\n');
            CallType ct = CHALLENGE;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            if (send(client_fd, &opponent_id, sizeof(int), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            printf("En attente de la réponse de l'adversaire ...\n");
            pthread_cond_wait(&cond_challenge, &lock);
        }
        else if (choice == 4) {
            int user_profile_id;
            printf("Entrer l'id du joueur: ");
            if (scanf("%d", &user_profile_id) != 1) {
                printf("Entrée invalide.\n");
                while (getchar() != '\n');
                continue;
            }
            while (getchar() != '\n');
            CallType ct = CONSULT_USER_PROFILE;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            if (send(client_fd, &user_profile_id, sizeof(int), 0) <= 0) {
                perror("send failed");
                exit(EXIT_FAILURE);
            }
            pthread_cond_wait(&cond_user_profile, &lock);
        }

        else if (choice == 5) {
            close(client_fd);
            printf("Déconnecté.\n");
            exit(EXIT_SUCCESS);
        }
        else {
            printf("Choix invalide.\n");
        }
    }

    return 0;
}
