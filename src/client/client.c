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
pthread_mutex_t lock_turn = PTHREAD_MUTEX_INITIALIZER;

User user;

// challenge variables (challenge received)
int pending_challenge = 0;
int challenger_id_global = -1;
char challenger_username_global[USERNAME_SIZE + 1] = {0};
int client_fd_global;

// challenge variables (sent)
int sent_challenges = 0;

// ongoing game variables
pthread_cond_t cond_game = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_game_turn = PTHREAD_COND_INITIALIZER;

int in_game = 0;
int last_move = -1;
int game_over = -1;
Game *game = NULL;

// selected input in the terminal
int choice_input = -1;

void clear_terminal() {
    printf("\033[2J\033[H"); // efface l’écran et remet le curseur en haut
    fflush(stdout);
}

void *play_game(void *arg) {
    int client_fd = *(int *)arg;
    // 1 for player 1, 2 for player 2
    int order = 0;

    Player me = newPlayer(user.id, client_fd);
    Player opponent = newPlayer(-1, -1); // unknown for now and not needed
    printf("Joueurs créés\n");
    while (1) {
        pthread_mutex_lock(&lock_turn);
        pthread_cond_wait(&cond_game_turn, &lock_turn);
        if (game_over != -1) {
            if (game_over == 1) {
                printf("\n>>> Vous avez gagné la partie ! Félicitations !\n");
            } else if (game_over == 0) {
                printf("\n>>> Vous avez perdu la partie. Bonne chance pour la prochaine fois !\n");
            } else {
                printf("\n>>> La partie s'est terminée par un match nul.\n");
            }
            // reset game variables
            in_game = 0;
            last_move = -1;
            game_over = -1;
            free(game);
            game = NULL;
            pthread_mutex_unlock(&lock_turn);
            return NULL;
        }
        int move_played = last_move;
        if (game == NULL) {
            // it means it's the first turn of the game and we are the first player
            if (move_played == -1) {
                game = newGame(&me, &opponent);
                order = 1;
            } else {
                game = newGame(&opponent, &me);
                order = 2;
            }
            printf("\n>>> La partie commence ! Vous êtes le joueur %d.\n", order);
        }


        printf("\n>>> C'est votre tour de jouer !\n");
        if (move_played != -1) {
            printf("Le dernier coup joué était la position %d.\n", move_played);
            // update the game state with the last move played by the opponent
            int adjusted_position = (order == 2) ? move_played + 5 : move_played - 1;
            moveSeeds(game, adjusted_position);
            int opponent_order = (order == 1) ? 2 : 1;
            opponent.score += collectSeedsAndCountPoints(game, adjusted_position, opponent_order);


            printf("SCORE : Vous %d - Adversaire %d\n", me.score, opponent.score);
        } else {
            printf("Vous êtes le premier à jouer.\n");
        }

        printGame(game, order);


        printf("ENTREZ UNE POSITION DE CASE POUR DÉPLACER LES GRAINES\n");
        scanf("%d", &choice_input);

        // if exceds 5 of <0
        while (choice_input < 1 || choice_input > 6) {
            printf("PLAYER 1 - ENTER A CELL POSITION BETWEEN 1 AND 6: ");
            scanf("%d", &choice_input);
        }

        // if the player has 0 seeds in that hole, he needs to select another one
        int adjusted_position = (order == 1) ? choice_input - 1 : choice_input + 5;
        while (game->board[adjusted_position] == 0) {
            printf("PLAYER 1 - ENTER A CELL POSITION WHERE YOU HAVE MORE THAN 1 SEED : ");
            scanf("%d", &choice_input);
            adjusted_position = (order == 1) ? choice_input - 1 : choice_input + 5;
        }
        // add our points
        int position_of_last_put_seed = moveSeeds(game, adjusted_position);
        me.score += collectSeedsAndCountPoints(game, position_of_last_put_seed, order);

        CallType ct = PLAY_MADE;
        send(client_fd, &ct, sizeof(ct), 0);
        send(client_fd, &choice_input, sizeof(choice_input), 0);
        pthread_mutex_unlock(&lock_turn);
        printf("\n>>> Coup joué : position %d envoyée au serveur.\n", choice_input);


        return NULL;
    }
}


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
            printf("\n>>> Défi reçu de %s (id=%d)\n", challenger_username_global, challenger_id_global);
            printf("Accepter le défi ? (1 = Oui / 0 = Non) : ");
            pending_challenge = 1;
            pthread_mutex_unlock(&lock);
        }

        else if (incoming == CHALLENGE_REQUEST_ANSWER) {
            int challenged_user_id;
            int answer = -1;
            recv(client_fd, &challenged_user_id, sizeof(int), 0);
            recv(client_fd, &answer, sizeof(int), 0);

            if (answer == 0) {
                printf("\n>>> Votre défi à l'utilisateur %d a été refusé.\n", challenged_user_id);
            }
            else {
                printf("\n>>> Votre défi à l'utilisateur %d a été accepté !", challenged_user_id);
            }
            sent_challenges--;
        }

        else if (incoming == ERROR) {
            char error_msg[256] = {0};
            int previous_call;
            recv(client_fd, &previous_call, sizeof(int), 0);
            recv(client_fd, error_msg, sizeof(error_msg), 0);
            printf(">>> Erreur : %s\n", error_msg);
            switch (previous_call) {
                case CHALLENGE:
                    sent_challenges--;
                    break;
                case CONSULT_USER_PROFILE:
                    pthread_cond_signal(&cond_user_profile);
                    break;
                case CHALLENGE_REQUEST_ANSWER:
                    // nothing to do here for now
                    break;
                case LIST_USERS:
                    pthread_cond_signal(&cond_list_users);
                    break;

                default:
                    break;
            }
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
        else if (incoming == CHALLENGE_START) {
            pthread_mutex_lock(&lock);
            char opponent_username[USERNAME_SIZE + 1] = {0};
            recv(client_fd, opponent_username, USERNAME_SIZE + 1, 0);
            clear_terminal();
            printf("\n>>> Le défi avec %s commence maintenant !\n", opponent_username);

            in_game = 1;

            pthread_t game_thread;
            // start the game in a new thread
            pthread_create(&game_thread, NULL, play_game, &client_fd);
            pthread_mutex_unlock(&lock);
        }

        else if (incoming == YOUR_TURN) {
            int move_played;
            recv(client_fd, &move_played, sizeof(move_played), 0);
            printf("\n>>> Le serveur a notifié que c'est votre tour de jouer.\n");
            pthread_mutex_lock(&lock);
            last_move = move_played;
            pthread_cond_signal(&cond_game_turn);
            pthread_mutex_unlock(&lock);
        }
        else if (incoming == GAME_OVER) {
            int result;
            recv(client_fd, &result, sizeof(result), 0);
            printf("\n>>> Le serveur a notifié que la partie est terminée.\n");
            pthread_mutex_lock(&lock);
            game_over = result;
            in_game = 0;
            pthread_mutex_unlock(&lock);
        }


        else {
            printf("\n>>> Message inconnu reçu du serveur.\n");
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

    while (choice_input != 0 && choice_input != 1) {
        printf("1 pour accepter, 0 pour refuser : ");
        if (scanf("%d", &choice_input) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');
    }

    CallType ct = CHALLENGE_REQUEST_ANSWER;
    if (send(client_fd_global, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd_global, &challenger_id, sizeof(challenger_id), 0) <= 0) perror("send id");
    if (send(client_fd_global, &choice_input, sizeof(choice_input), 0) <= 0) perror("send choix");

    if (choice_input == 1) {
        // if accepted, we wait for the server to start the game
        printf(">>> Vous avez accepté le défi de %s !\n", challenger_name);
        pthread_mutex_lock(&lock);
        in_game = 1;
        pthread_mutex_unlock(&lock);

    }

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
        // let the chance for incoming messages from the server to arrive and be displayed before showing the menu again
        sleep(0.5);
        pthread_mutex_lock(&lock);
        while (in_game) {
            pthread_cond_wait(&cond_game, &lock);
        }
        pthread_mutex_unlock(&lock);


        // handle incoming changes will set pending_challenge to 1 if there is a challenge that has been accepted
        handle_incoming_challenge();

        // if we are in a game, we skip the menu and play the game
        //play_game();

        if (in_game){
            // in this case, the input must be used for the game and not for the menu
            continue;
        }

        printf("\n===== MENU =====\n");
        printf(" 1 - Voir votre profil\n");
        printf(" 2 - Afficher les utilisateurs en ligne\n");
        printf(" 3 - Défier un utilisateur\n");
        printf(" 4 - Consulter le profil d'un utilisateur\n");
        printf(" 5 - Quitter\n");
        printf("Votre choix: ");

        if (scanf("%d", &choice_input) != 1) {
            printf("Entrée invalide.\n");
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');

        if (pending_challenge){
            // in this case, the input must be used to answer the challenge and not for the menu
            continue;
        }

        if (in_game){
            // when the challenger has his challenge accepted, we enter the game directly
            continue;
        }
        if (choice_input == 1) {
            printUser(&user);
        }

        else if (choice_input == 2) {
            CallType ct = LIST_USERS;
            if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
            pthread_mutex_lock(&lock);
            pthread_cond_wait(&cond_list_users, &lock);
            pthread_mutex_unlock(&lock);
        }

        else if (choice_input == 3) {
            if (sent_challenges){
                printf("Vous avez déjà envoyé une demande, veuillez attendre la réponse avant d'en faire de nouvelles\n");
                continue;
            }
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
            sent_challenges++;
            printf(">>> En attente de la réponse de l'adversaire...\n");
        }

        else if (choice_input == 4) {
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

        else if (choice_input == 5) {
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



