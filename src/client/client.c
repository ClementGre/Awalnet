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
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

User user;

// challenge variables
int pending_challenge = 0;
int challenger_id_global = -1;
char challenger_username_global[USERNAME_SIZE + 1] = {0};
int client_fd_global;


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
            recv(client_fd, &challenger_id_global, sizeof(int), 0);
            recv(client_fd, challenger_username_global, USERNAME_SIZE + 1, 0);
            pending_challenge = 1;
            pthread_cond_signal(&cond_challenge);
            pthread_mutex_unlock(&lock);
        }

        else if (incoming == CHALLENGE_REQUEST_ANSWER) {
            int challenged_user_id;
            int answer = -1;
            recv(client_fd, &challenged_user_id, sizeof(int), 0);
            recv(client_fd, &answer, sizeof(int), 0);

            if (answer == 0)
                printf(">>> Votre défi à l'utilisateur %d a été refusé.\n", challenged_user_id);
            else
                printf(">>> Votre défi à l'utilisateur %d a été accepté !\n", challenged_user_id);
        }

        else if (incoming == ERROR) {
            char error_msg[256] = {0};
            recv(client_fd, error_msg, sizeof(error_msg), 0);
            printf(">>> Erreur : %s\n", error_msg);
            pthread_cond_signal(&cond_list_users);
            pthread_cond_signal(&cond_user_profile);
        }

        else if (incoming == SUCCESS) {
            printf(">>> Votre demande a été envoyée avec succès.\n");
            pthread_cond_signal(&cond_list_users);
        }

        else if (incoming == LIST_USERS) {
            char user_list_buffer[1024] = {0};
            recv(client_fd, user_list_buffer, sizeof(user_list_buffer), 0);
            printf("Utilisateurs en ligne :\n%s\n", user_list_buffer);
            pthread_cond_signal(&cond_list_users);
        }

        else if (incoming == CONSULT_USER_PROFILE) {
            CallType ct = SENT_USER_PROFILE;
            int request_user_id;
            recv(client_fd, &request_user_id, sizeof(int), 0);

            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
            if (send(client_fd, &request_user_id, sizeof(int), 0) <= 0) perror("send id");

            uint8_t user_buffer[1024] = {0};
            serialize_User(&user, user_buffer);
            if (send(client_fd, &user_buffer, sizeof(user_buffer), 0) <= 0)
                perror("send user");
        }

        else if (incoming == RECEIVE_USER_PROFILE) {
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

void handle_incoming_challenge() {
    pthread_mutex_lock(&lock);
    if (!pending_challenge) {
        pthread_mutex_unlock(&lock);
        return;
    }

    int challenger_id = challenger_id_global;
    char challenger_name[USERNAME_SIZE + 1];
    strcpy(challenger_name, challenger_username_global);
    pending_challenge = 0;
    pthread_mutex_unlock(&lock);

    printf("\n>>> Défi reçu de %s (id=%d)\n", challenger_name, challenger_id);

    int choix = -1;
    while (choix != 0 && choix != 1) {
        printf("Accepter le défi ? (1 = Oui / 0 = Non) : ");
        if (scanf("%d", &choix) != 1) {
            printf("Entrée invalide.\n");
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');
    }

    CallType ct = CHALLENGE_REQUEST_ANSWER;
    if (send(client_fd_global, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd_global, &challenger_id, sizeof(challenger_id), 0) <= 0) perror("send id");
    if (send(client_fd_global, &choix, sizeof(choix), 0) <= 0) perror("send choix");

    if (choix == 1)
        printf(">>> Vous avez accepté le défi de %s !\n", challenger_name);
    else
        printf(">>> Vous avez refusé le défi de %s.\n", challenger_name);
}

int start_client() {
    int client_fd;
    struct sockaddr_in serv_addr;

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    client_fd_global = client_fd;

    // get username
    char username[USERNAME_SIZE + 1];
    printf("Entrez votre nom d'utilisateur: ");
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
        handle_incoming_challenge();

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
        while (getchar() != '\n');

        if (choice == 1) {
            printUser(&user);
        }

        else if (choice == 2) {
            CallType ct = LIST_USERS;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
            pthread_mutex_lock(&lock);
            pthread_cond_wait(&cond_list_users, &lock);
            pthread_mutex_unlock(&lock);
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
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
            if (send(client_fd, &opponent_id, sizeof(int), 0) <= 0) perror("send failed");
            printf(">>> En attente de la réponse de l'adversaire...\n");
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
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
            if (send(client_fd, &user_profile_id, sizeof(int), 0) <= 0) perror("send failed");

            pthread_mutex_lock(&lock);
            pthread_cond_wait(&cond_user_profile, &lock);
            pthread_mutex_unlock(&lock);
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
