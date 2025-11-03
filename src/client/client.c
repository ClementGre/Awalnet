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

// TODO : vider les entrées lues dans le jeu pour pas qu'elles soient interprétées par le tour suivant
// TODO : fix la logique des points qui est broken.

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
int your_turn = 0;
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
    free(arg);

    Player me = newPlayer(user.id, client_fd);
    Player opponent = newPlayer(-1, -1);
    int order = 0;

    while (1) {
        pthread_mutex_lock(&lock_turn);
        // attendre que ce soit notre tour OU que la partie soit finie
        while (!your_turn) {
            pthread_cond_wait(&cond_game_turn, &lock_turn);
        }

        // si la partie s'est terminée, on sort correctement
        if (game_over != -1) {
            int result = game_over;
            pthread_mutex_unlock(&lock_turn);

            if (result == 1) printf("\n>>> Vous avez gagné la partie ! Félicitations !\n");
            else if (result == 0) printf("\n>>> Vous avez perdu la partie.\n");
            else printf("\n>>> Match nul.\n");

            // nettoyage (sous lock si besoin)
            pthread_mutex_lock(&lock_turn);
            in_game = 0;
            last_move = -1;
            game_over = -1;
            free(game);
            game = NULL;
            pthread_mutex_unlock(&lock_turn);
            return NULL;
        }

        // ici: on est dans la partie et potentiellement notre tour (last_move a été mis)
        int move_played = last_move;
        if (game == NULL) {
            if (move_played == -1) {
                game = newGame(&me, &opponent);
                order = 1;
            } else {
                game = newGame(&opponent, &me);
                order = 2;
            }
            printf("\n>>> La partie commence ! Vous êtes le joueur %d.\n", order);
        }

        // copier localement puis unlock pour faire I/O
        pthread_mutex_unlock(&lock_turn);

        // appliquer le dernier coup si présent
        if (move_played != -1) {
            printf("Le dernier coup joué par votre adversaire était la position %d.\n", move_played);
            int opponent_order = (order == 1) ? 2 : 1;
            int adjusted_position = (opponent_order == 2) ? move_played + 5 : move_played - 1;
            moveSeeds(game, adjusted_position);
            opponent.score += collectSeedsAndCountPoints(game, adjusted_position, opponent_order);
        } else {
            printf("Vous êtes le premier à jouer.\n");
        }

        printGame(game, order);

        // lecture utilisateur : utiliser fgets pour éviter conflit scanf, stocker dans variable locale
        char input_buf[32];
        int local_choice = -1;

        while (1) {
            printf("ENTREZ UNE POSITION DE CASE POUR DÉPLACER LES GRAINES (1-6) : ");
            if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
                clearerr(stdin);
                continue;
            }
            if (sscanf(input_buf, "%d", &local_choice) != 1) continue;
            if (local_choice >= 1 && local_choice <= 6) break;
        }

        int adjusted_position = (order == 1) ? local_choice - 1 : local_choice + 5;
        while (game->board[adjusted_position] == 0) {
            printf("Pas de graines dans cette case. Choisissez une autre case.\n");
            if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) { clearerr(stdin); continue; }
            if (sscanf(input_buf, "%d", &local_choice) != 1) continue;
            adjusted_position = (order == 1) ? local_choice - 1 : local_choice + 5;
        }

        int position_of_last_put_seed = moveSeeds(game, adjusted_position);
        me.score += collectSeedsAndCountPoints(game, position_of_last_put_seed, order);

        // envoi au serveur
        CallType ct = PLAY_MADE;
        if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
        // we send the local choice to the server, which then has to convert it
        if (send(client_fd, &local_choice, sizeof(local_choice), 0) <= 0) perror("send choice");
        your_turn = 0;

        printf("\n>>> Coup joué : position %d envoyée au serveur.\n", local_choice);
        printf("Nouveau board : \n");
        printGame(game, order);
        printf("en attente de l'adversaire ...\n");

    }
    return NULL;
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
            pthread_mutex_lock(&lock_turn);
            char opponent_username[USERNAME_SIZE + 1] = {0};
            recv(client_fd, opponent_username, USERNAME_SIZE + 1, 0);
            clear_terminal();
            printf("\n>>> Le défi avec %s commence maintenant !\n", opponent_username);

            in_game = 1;

            int *fd_arg = malloc(sizeof(int));
            *fd_arg = client_fd;
            pthread_t game_thread;
            if (pthread_create(&game_thread, NULL, play_game, fd_arg) != 0) {
                perror("pthread_create");
                free(fd_arg);
            } else {
            }
            printf(">>> Thread de jeu démarré.\n");
            pthread_mutex_unlock(&lock_turn);
        }

        else if (incoming == YOUR_TURN) {
            int move_played;
            recv(client_fd, &move_played, sizeof(move_played), 0);
            printf("\n>>> Le serveur a notifié que c'est votre tour de jouer.\n");
            pthread_mutex_lock(&lock_turn);
            your_turn = 1;
            last_move = move_played;
            pthread_cond_signal(&cond_game_turn);
            pthread_mutex_unlock(&lock_turn);
        }

        else if (incoming == GAME_OVER) {
            int result;
            recv(client_fd, &result, sizeof(result), 0);
            printf("\n>>> Le serveur a notifié que la partie est terminée.\n");

            pthread_mutex_lock(&lock_turn);
            game_over = result;
            in_game = 0;
            // réveiller le thread jeu au cas où il attendait
            pthread_cond_signal(&cond_game_turn);
            pthread_mutex_unlock(&lock_turn);
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



