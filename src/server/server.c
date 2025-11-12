#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include "../common/api.h"

#define PORT 8080
#define MAX_CLIENTS 10
#define USERNAME_SIZE 32
#define MAX_GAMES 20


typedef struct {
    int fd;
    int user_id;
    char username[USERNAME_SIZE + 1];
    int active;
    int in_game;
    int nb_of_pending_challenges;
    int *pending_challenge_from_user_fd;
} Client;

Client clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

int find_client_index_by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].fd == fd) return i;
    }
    return -1;
}

void remove_client_by_index(int idx) {
    if (idx < 0 || idx >= MAX_CLIENTS) return;

    pthread_mutex_lock(&clients_mutex);

    if (!clients[idx].active) {
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    close(clients[idx].fd);

    if (clients[idx].pending_challenge_from_user_fd) {
        free(clients[idx].pending_challenge_from_user_fd);
        clients[idx].pending_challenge_from_user_fd = NULL;
    }

    clients[idx].active = 0;
    clients[idx].in_game = 0;
    clients[idx].nb_of_pending_challenges = 0;
    clients[idx].fd = -1;

    pthread_mutex_unlock(&clients_mutex);
}

size_t send_payload(CallType calltype, u_int8_t *payload, size_t payload_size, int fd) {
    send(fd, &calltype, sizeof(calltype), 0);
    send(fd, &payload_size, sizeof(int), 0);
    return send(fd, payload, payload_size, 0);

}

size_t  end_error(CallType calltype, const char *error_msg, int fd) {

    size_t msg_len = strlen(error_msg) + 1;
    size_t total_size = sizeof(CallType) + msg_len;

    uint8_t buffer[total_size];

    // Copie du calltype d’origine
    memcpy(buffer, &calltype, sizeof(CallType));

    // Copie du message d’erreur juste après
    memcpy(buffer + sizeof(CallType), error_msg, msg_len);

    return send_payload(ERROR, buffer, total_size, fd);

}


// ---------------------- GAME LOGIC ---------------------- //
typedef struct {
    Game *game;
    int running;
    int game_id;
    pthread_t thread;
    pthread_mutex_t mutex;
} GameInstance;

static GameInstance *games[MAX_GAMES] = {0};
static int next_game_id = 1;

GameInstance *alloc_game(void) {
    for (int i = 0; i < MAX_GAMES; ++i) {
        if (games[i] == NULL) {
            GameInstance *g = calloc(1, sizeof(GameInstance));
            g->game_id = next_game_id++;
            pthread_mutex_init(&g->mutex, NULL);
            g->running = 1;
            games[i] = g;
            return g;
        }
    }
    return NULL;
}

void free_game(GameInstance *g) {
    if (!g){
        return;
    }
    for (int i = 0; i < MAX_GAMES; ++i) {
        if (games[i] == g) {
            games[i] = NULL;
            break;
        }
    }
    pthread_mutex_destroy(&g->mutex);
    free(g);
}



void *game_thread(void *arg) {
    GameInstance *g = (GameInstance *)arg;

    int tours = 0;
    size_t n;
    int move_made = -1;
    while (g->running && tours <= MAX_ROUNDS) {

        // if tours%2 ==0 player 1 else player 2
        // FIRST NOTIFY THE CURRENT PLAYER IT'S THEIR TURN
        Player current_player = (tours % 2 == 0) ? g->game->player1 : g->game->player2;
        CallType your_turn = YOUR_TURN;
        //n = send(current_player.fd, &your_turn, sizeof(your_turn), 0);
        // the first player will receive the number - 1 indicating that he is the first to play, the second will receive any positive number
        //send(current_player.fd, &move_made, sizeof(move_made), 0);
        n = send_payload(your_turn, move_made, sizeof(move_made), current_player.fd);
        printf("Sent YOUR_TURN to player fd %d for game %d\n", current_player.fd, g->game_id);
        if (n <= 0) {
            printf("Player fd %d disconnected, ending game %d\n", current_player.fd, g->game_id);
            int idx = find_client_index_by_fd(current_player.fd);
            if (idx != -1) remove_client_by_index(idx);

            int opponent_fd = (tours % 2 == 0) ? g->game->player2.fd : g->game->player1.fd;
            int opp_idx = find_client_index_by_fd(opponent_fd);
            if (opp_idx != -1) {
                pthread_mutex_lock(&clients_mutex);
                clients[opp_idx].in_game = 0;
                pthread_mutex_unlock(&clients_mutex);

                CallType go = GAME_OVER;
                GAME_OVER_REASON gameOverReason = OPPONENT_DISCONNECTED;
                send_payload(go, &gameOverReason, sizeof(gameOverReason), opponent_fd);
                //send(opponent_fd, &go, sizeof(go), 0);
            }
            break;
        }

        // THEN WAIT FOR A MOVE FROM THAT PLAYER
        CallType incoming;
        n = recv(current_player.fd, &incoming, sizeof(incoming), 0);
        if (n <= 0) {
            printf("Player fd %d disconnected, ending game %d\n", current_player.fd, g->game_id);
            int idx = find_client_index_by_fd(current_player.fd);
            if (idx != -1) remove_client_by_index(idx);
            int opponent_fd = (tours % 2 == 0) ? g->game->player2.fd : g->game->player1.fd;
            int opp_idx = find_client_index_by_fd(opponent_fd);
            if (opp_idx != -1) {
                pthread_mutex_lock(&clients_mutex);
                clients[opp_idx].in_game = 0;
                pthread_mutex_unlock(&clients_mutex);

                CallType go = GAME_OVER;
                GAME_OVER_REASON gameOverReason = OPPONENT_DISCONNECTED;
                send_payload(go, &gameOverReason, sizeof(gameOverReason), opponent_fd);
                /*send(opponent_fd, &go, sizeof(go), 0);
                send(opponent_fd, &gameOverReason, sizeof(gameOverReason), 0);*/

            }
            break;
        }

        if (incoming == PLAY_MADE) {
            // recv the move
            n = recv(current_player.fd, &move_made, sizeof(move_made), 0);
            printf("Game %d received move %d from player fd %d\n", g->game_id, move_made, current_player.fd);
            if (n <= 0) {
                perror("recv move");
                break;
            }
        }
        else {
            // Unexpected message: ignore or log
            printf("Game %d received unexpected pack from fd %d: %d\n", g->game_id, current_player.fd, incoming);
        }

        // then process the move to update to board and scores
        if (tours % 2 == 0) {
            // if the first player played
            int position_of_last_put_seed = moveSeeds(g->game, move_made  - 1);
            g->game->player1.score += collectSeedsAndCountPoints(g->game, position_of_last_put_seed, 1);
            printf("SCORE : Player 1: %d | Player 2: %d  (game %d)\n", g->game->player1.score, g->game->player2.score, g->game_id);
        }
        else {
            // the second player made the last move
            int position_of_last_put_seed = moveSeeds(g->game, move_made + 5);
            g->game->player2.score += collectSeedsAndCountPoints(g->game, position_of_last_put_seed, 2);
            printf("SCORE : Player 1: %d | Player 2: %d  (game %d)\n", g->game->player1.score, g->game->player2.score, g->game_id);

        }

        // then checks for win conditions
        if (g->game->player1.score == WINNING_SCORE || playerSeedsLeft(g->game, 2) < 6) {
            printf("\n---------------- PLAYER 1 WON !!! --------------\n");
            CallType go = GAME_OVER;
            GAME_OVER_REASON win = WIN;
            GAME_OVER_REASON lose = LOSE;
            send_payload(go, &win, sizeof(win), g->game->player1.fd);
            send_payload(go, &lose, sizeof(lose), g->game->player2.fd);
            /*send(g->game->player1.fd, &go, sizeof(go), 0);
            send(g->game->player1.fd, &win, sizeof(win), 0);
            send(g->game->player2.fd, &go, sizeof(go), 0);
            send(g->game->player2.fd, &lose, sizeof(lose), 0);*/

            int idx1 = find_client_index_by_fd(g->game->player1.fd);
            int idx2 = find_client_index_by_fd(g->game->player2.fd);
            if (idx1 != -1) {
                pthread_mutex_lock(&clients_mutex);
                clients[idx1].in_game = 0;
                pthread_mutex_unlock(&clients_mutex);
            }
            if (idx2 != -1) {
                pthread_mutex_lock(&clients_mutex);
                clients[idx2].in_game = 0;
                pthread_mutex_unlock(&clients_mutex);
            }

            break;
        }
        if (g->game->player2.score == WINNING_SCORE || playerSeedsLeft(g->game, 1) < 6) {
            printf("\n---------------- PLAYER 2 WON !!! --------------\n");

            CallType go = GAME_OVER;
            GAME_OVER_REASON win = WIN;
            GAME_OVER_REASON lose = LOSE;
            send_payload(go, &lose, sizeof(lose), g->game->player1.fd);
            send_payload(go, &win, sizeof(win), g->game->player2.fd);
            /*send(g->game->player1.fd, &go, sizeof(go), 0);
            send(g->game->player1.fd, &lose, sizeof(lose), 0);
            send(g->game->player2.fd, &go, sizeof(go), 0);
            send(g->game->player2.fd, &win, sizeof(win), 0);*/

            break;
        }

        tours++;

    }

    // we end the game by freeing the game instance but before that we need to set the clients as not in game anymore
    int idx1 = find_client_index_by_fd(g->game->player1.fd);
    int idx2 = find_client_index_by_fd(g->game->player2.fd);
    if (idx1 != -1) {
        pthread_mutex_lock(&clients_mutex);
        clients[idx1].in_game = 0;
        pthread_mutex_unlock(&clients_mutex);
    }
    if (idx2 != -1) {
        pthread_mutex_lock(&clients_mutex);
        clients[idx2].in_game = 0;
        pthread_mutex_unlock(&clients_mutex);
    }

    // envoyer un message aux deux joueurs pour indiquer la fin de la partie

    if (tours > MAX_ROUNDS) {
        CallType go = GAME_OVER;
        GAME_OVER_REASON gameOverReason = DRAW;
        send_payload(go, &gameOverReason, sizeof(gameOverReason), g->game->player1.fd);
        send_payload(go, &gameOverReason, sizeof(gameOverReason), g->game->player2.fd);
        /*
        send(g->game->player1.fd, &go, sizeof(go), 0);
        send(g->game->player1.fd, &gameOverReason, sizeof(gameOverReason), 0);
        send(g->game->player2.fd, &go, sizeof(go), 0);
        send(g->game->player2.fd, &gameOverReason, sizeof(gameOverReason), 0);*/
        printf("Game %d ended in a draw due to max rounds reached\n", g->game_id);
    }

    free_game(g);


    return NULL;
}

// ---------------------- GAME LOGIC ---------------------- //


int start_server(void) {
    printf("🚀 Starting Awalnet server...\n");
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);


    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].active = 0;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);

    }

    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    printf("✨ Server listening on port %d\n", PORT);
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &read_fds);
                if (clients[i].fd > max_fd) max_fd = clients[i].fd;
            }
        }


        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("select failed");
            continue;
        }
        else if (activity == 0) {
            continue;
        }

        // new connection
        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                perror("accept failed");
                continue;
            }
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].active) {
                    clients[i].fd = new_socket;
                    clients[i].active = 1;
                    clients[i].in_game = 0;
                    clients[i].nb_of_pending_challenges = 0;
                    // we could optimize memory by reallocating when a new challenge is received (to nb_of_pending_challenges + 1) but flemme
                    clients[i].pending_challenge_from_user_fd = malloc(sizeof(int) * (MAX_CLIENTS - 1));
                    break;
                }
            }
        }

        // Données clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) continue;
            if (!FD_ISSET(clients[i].fd, &read_fds)) continue;
            // if client is in game, handle game messages and ignore others
            if (clients[i].in_game) continue;

            CallType call_type;
            ssize_t n = read(clients[i].fd, &call_type, sizeof(CallType));
            if (n <= 0) {
                printf("Client fd %d disconnected (main loop)\n", clients[i].fd);
                remove_client_by_index(i);
                continue;
            }

            switch (call_type) {
                case CONNECT: {
                    char username[USERNAME_SIZE + 1] = {0};
                    if (read(clients[i].fd, username, USERNAME_SIZE + 1) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    User user = newUser(username, "");
                    printf("New user connected: %s (%d) - socket %d\n", username, user.id, clients[i].fd);
                    clients[i].user_id = user.id;
                    strncpy(clients[i].username, username, USERNAME_SIZE);

                    uint8_t user_buffer[1024] = {0};
                    serialize_User(&user, user_buffer);
                    CallType out = CONNECT_CONFIRM;
                    //send(clients[i].fd, &out, sizeof(out), 0);
                    //send(clients[i].fd, user_buffer, sizeof(user_buffer), 0);
                    send_payload(out, user_buffer, sizeof(user_buffer), clients[i].fd);
                    break;
                }
                case CHALLENGE: {
                    CallType error = ERROR;
                    int opponent_user_id = 0;
                    if (read(clients[i].fd, &opponent_user_id, sizeof(int)) <= 0) {
                        close(clients[i].fd); clients[i].active = 0; break;
                    }
                    if(opponent_user_id == clients[i].user_id) {
                        printf("User %s (id=%d) attempted to challenge themselves. Ignored.\n",
                               clients[i].username, clients[i].user_id);
                        char error_msg[] = "You cannot challenge yourself.";
                        send_error(call_type, error_msg, clients[i].fd);
                        /*send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &call_type, sizeof(call_type), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);*/
                        break;
                    }
                    // Find target client by user_id and send challenge
                    int target = -1;
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].active && clients[j].user_id == opponent_user_id) {
                            target = j;
                            break;
                        }
                    }
                    if (target != -1) {
                        // if a player is found, send challenge request except if he is already in a game
                        if (clients[target].in_game) {
                            printf("User %s (id=%d) attempted to challenge user %s (id=%d) who is already in a game. Ignored.\n",
                                   clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id);
                            char error_msg[] = "The player challenged is currently in a game.";
                            send_error(call_type, error_msg, clients[i].fd);
                            /*send(clients[i].fd, &error, sizeof(error), 0);
                            send(clients[i].fd, &call_type, sizeof(call_type), 0);
                            send(clients[i].fd, error_msg, sizeof(error_msg), 0);*/
                            break;
                        }
                        CallType out = CHALLENGE;
                        clients[target].pending_challenge_from_user_fd[clients[target].nb_of_pending_challenges] = clients[i].fd;
                        clients[target].nb_of_pending_challenges++;
                        printf("User %s (id=%d) has %d pending challenges.\n",  clients[target].username, clients[target].user_id, clients[target].nb_of_pending_challenges);
                        // we need to send the info in a buffer like this:
                        uint8_t buffer[sizeof(int) + USERNAME_SIZE + 1];
                        memcpy(buffer, &clients[i].user_id, sizeof(int));
                        memcpy(buffer + sizeof(int), clients[i].username, USERNAME_SIZE + 1);
                        send_payload(out, buffer, sizeof(buffer), clients[target].fd);
                        /*send(clients[target].fd, &out, sizeof(out), 0);
                        send(clients[target].fd, &clients[i].user_id, sizeof(int), 0);
                        send(clients[target].fd, clients[i].username, USERNAME_SIZE + 1, 0);*/
                        printf("Challenge initialized by de %s(id=%d) to %s(id=%d) | socket %d to bind\n",
                               clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id, clients[target].fd);

                    } else {
                        printf("Utilisateur %d introuvable pour challenge.\n", opponent_user_id);
                        char error_msg[] = "User not found or not online.";
                        int previous_call = CHALLENGE;
                        send_error(previous_call, error_msg, clients[i].fd);
                        /*
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
                        */
                    }
                    break;
                }
                case CHALLENGE_REQUEST_ANSWER: {
                    CallType error = ERROR;
                    int request_user_id = 0;
                    if (read(clients[i].fd, &request_user_id, sizeof(int)) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }

                    int answer = - 1;
                    if (read(clients[i].fd, &answer, sizeof(answer)) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    // Find target client by user_id and send answer
                    int target = -1;
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].active && clients[j].user_id == request_user_id) {
                            target = j;
                            break;
                        }
                    }
                    if (target != -1) {
                        // if the player that initiated the challenge is playing a game now, we cannot send the answer and have to notify the challenged that the challenge he accepted no longer exists.
                        if (clients[target].in_game) {
                            char error_msg[] = "The player who challenged you is now in a game.";
                            int previous_call = CHALLENGE_REQUEST_ANSWER;
                            send_error(previous_call, error_msg, clients[i].fd);
                            /*
                            send(clients[i].fd, &error, sizeof(error), 0);
                            send(clients[i].fd, &previous_call, sizeof(previous_call), 0);
                            send(clients[i].fd, error_msg, sizeof(error_msg), 0);
                             */
                            break;
                        }

                        CallType out = CHALLENGE_REQUEST_ANSWER;
                        // send answer to selected challenger
                        // store in a buffer the user_id of the challenged and the answer
                        uint8_t buffer[sizeof(int) + sizeof(int)];
                        memcpy(buffer, &clients[i].user_id, sizeof(int));
                        memcpy(buffer + sizeof(int), &answer, sizeof(int));
                        send_payload(out, buffer, sizeof(buffer), clients[target].fd);
                        /*
                        send(clients[target].fd, &out, sizeof(out), 0);
                        send(clients[target].fd, &clients[i].user_id, sizeof(int), 0);
                        send(clients[target].fd, &answer, sizeof(int), 0);
                         */
                        if (answer == 1) {
                            // challenge accepted -> notify awaiting challengers that were not selected
                            for (int k = 0; k < clients[i].nb_of_pending_challenges; k++) {
                                if (clients[i].pending_challenge_from_user_fd[k] != clients[target].fd) {
                                    int fd_to_notify = clients[i].pending_challenge_from_user_fd[k];
                                    CallType notify = CHALLENGE_REQUEST_ANSWER;
                                    int refused = 0;
                                    // store in a buffer the user_id of the challenged and the refusal
                                    uint8_t buffer2[sizeof(int) + sizeof(int)];
                                    memcpy(buffer2, &clients[i].user_id, sizeof(int));
                                    memcpy(buffer2 + sizeof(int), &refused, sizeof(int));
                                    send_payload(notify, buffer2, sizeof(buffer2), fd_to_notify);
                                    /*
                                    send(fd_to_notify, &notify, sizeof(notify), 0);
                                    send(fd_to_notify, &clients[i].user_id, sizeof(int), 0);
                                    send(fd_to_notify, &refused, sizeof(int), 0);
                                     */
                                    printf("Notified fd %d that challenge to %s(id=%d) was refused due to another acceptance.\n",
                                           fd_to_notify, clients[i].username, clients[i].user_id);
                                }
                            }
                            clients[target].nb_of_pending_challenges = 0;
                            printf("Challenge accepted by %s(id=%d) to %s(id=%d) | socket %d to bind\n",
                                   clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id, clients[target].fd);
                            CallType out = CHALLENGE_START;
                            // notify both clients that the challenge is starting now
                            send_payload(out, clients[target].username, sizeof(clients[target].username), clients[i].fd);

                            /*send(clients[i].fd, &out, sizeof(out), 0);
                            send(clients[i].fd, &clients[target].username, sizeof(clients[target].username), 0);
                             */

                            send_payload(out, clients[i].username, sizeof(clients[i].username), clients[target].fd);
                            /*send(clients[target].fd, &out, sizeof(out), 0);
                            send(clients[target].fd, &clients[i].username, sizeof(clients[i].username), 0);
                             */

                            // then mark both clients as in game
                            clients[i].in_game = 1;
                            clients[target].in_game = 1;

                            // randomly decide who starts
                            srand((unsigned int)time(NULL));
                            int starter = rand() % 2; // 0 or 1
                            Player player1, player2;
                            if (starter){
                                player1 = newPlayer(clients[i].user_id, clients[i].fd);
                                player2 = newPlayer(clients[target].user_id, clients[target].fd);
                            } else {
                                player1 = newPlayer(clients[target].user_id, clients[target].fd);
                                player2 = newPlayer(clients[i].user_id, clients[i].fd);
                            }

                            // Create game instance
                            Game* game = newGame(&player1, &player2);

                            // Then create a thread to handle the game logic
                            GameInstance *g = alloc_game();
                            if (!g) {
                                fprintf(stderr, "No game slot available\n");
                                break;
                            }
                            g->game = game;


                            // launch thread
                            if (pthread_create(&g->thread, NULL, game_thread, g) != 0) {
                                perror("pthread_create game_thread");
                                clients[i].in_game = clients[target].in_game = 0;
                                free_game(g);
                                break;
                            }

                            printf("Game %d created between %d and %d (fds %d & %d)\n",
                                   g->game_id, g->game->player1.user_id, g->game->player2.user_id, g->game->player1.fd, g->game->player2.fd);


                        }


                    } else {
                        printf("Utilisateur %d introuvable pour challenge.\n", request_user_id);
                        char error_msg[] = "User not found or not online.";
                        int previous_call = CHALLENGE_REQUEST_ANSWER;
                        send_error(previous_call, error_msg, clients[i].fd);
                        /*
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
                         */
                    }
                    break;
                }
                case LIST_USERS: {
                    CallType out = LIST_USERS;
                    char user_list_buffer[1024] = {0};
                    int client_count = 0;
                    for (int u = 0; u < MAX_CLIENTS; u++) {
                        if (clients[u].active && clients[u].user_id != 0 && clients[u].fd != clients[i].fd) {
                            strcat(user_list_buffer, clients[u].username);
                            strcat(user_list_buffer, " (id = ");
                            char id_str[12];
                            sprintf(id_str, "%d", clients[u].user_id);
                            strcat(user_list_buffer, id_str);
                            strcat(user_list_buffer, ")");
                            if (clients[u].in_game) {
                                strcat(user_list_buffer, " [IN GAME]");
                            }
                            strcat(user_list_buffer, "\n");
                            client_count++;
                        }
                    }
                    printf("Sending user list to %s (id=%d)\n", clients[i].username, clients[i].user_id);
                    if (client_count == 0) {
                        strcat(user_list_buffer, "No other users online.\n");
                    }
                    send_payload(out, user_list_buffer, strlen(user_list_buffer) + 1, clients[i].fd);
                    /*
                    send(clients[i].fd, &out, sizeof(out), 0);
                    send(clients[i].fd, user_list_buffer, strlen(user_list_buffer) + 1, 0);
                     */
                    break;
                }
                case LIST_ONGOING_GAMES: {
                    CallType out = LIST_ONGOING_GAMES;
                    char games_list_buffer[1024] = {0};
                    int games_count = 0;
                    for (int u = 0; u < next_game_id; u++) {
                        if (games[u] != NULL) {
                            char game_info[128];
                            sprintf(game_info, "Game %d: %d VS %d | %d - %d\n",
                                    games[u]->game_id,
                                    games[u]->game->player1.user_id,
                                    games[u]->game->player2.user_id,
                                    games[u]->game->player1.score,
                                    games[u]->game->player2.score);
                            strcat(games_list_buffer, game_info);
                            games_count++;
                        }
                    }
                    printf("Sending games list to %s (id=%d)\n", clients[i].username, clients[i].user_id);
                    if (games_count == 0) {
                        strcat(games_list_buffer, "No games are being played\n");
                    }
                    send_payload(out, games_list_buffer, strlen(games_list_buffer) + 1, clients[i].fd);
                    /*send(clients[i].fd, &out, sizeof(out), 0);
                    send(clients[i].fd, games_list_buffer, strlen(games_list_buffer) + 1, 0);
                     */
                    break;
                }
                case CONSULT_USER_PROFILE: {
                    CallType out = CONSULT_USER_PROFILE;
                    CallType error = ERROR;
                    int requested_user_id = 0;
                    if (read(clients[i].fd, &requested_user_id, sizeof(int)) <= 0) {
                        close(clients[i].fd); clients[i].active = 0; break;
                    }
                    if(requested_user_id == clients[i].user_id) {
                        printf("User %s (id = %d) attempted to request their own profile. Ignored.\n",
                               clients[i].username, clients[i].user_id);
                        char error_msg[] = "To view your own profile, press 1.";
                        int previous_call = CONSULT_USER_PROFILE;

                        send_error(previous_call, error_msg, clients[i].fd);
                        /*
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
                         */
                        break;
                    }
                    // Find target client by user_id and send them the request to send their profile
                    int target = -1;
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].active && clients[j].user_id == requested_user_id) {
                            target = j;
                            break;
                        }
                    }
                    if (target != -1) {
                        send_payload(out, &clients[i].user_id, sizeof(int), clients[target].fd);
                        /*
                        send(clients[target].fd, &out, sizeof(out), 0);
                        send(clients[target].fd, &clients[i].user_id, sizeof(int), 0);
                         */
                        printf("Request profile initialized by de %s(id=%d) to %s(id=%d) | socket %d to bind\n",
                               clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id, clients[target].fd);
                    } else {
                        printf("Utilisateur %d does not exist -> cannot send his profile\n", requested_user_id);
                        char error_msg[] = "User not found or not online.";
                        int previous_call = CONSULT_USER_PROFILE;
                        send_error(previous_call, error_msg, clients[i].fd);
                        /*
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
                         */
                    }
                    break;
                }
                case SENT_USER_PROFILE:{
                    CallType out = RECEIVE_USER_PROFILE;
                    int request_user_id;
                    if (recv(clients[i].fd, &request_user_id, sizeof (int), 0) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    // we get the user_profile serialized
                    uint8_t buffer[1024] = {0};
                    if (recv(clients[i].fd,  buffer, sizeof(buffer), 0) <= 0) {
                        perror("recv failed");
                        exit(EXIT_FAILURE);
                    }


                    // and then send it to the request_user_id
                    int target = -1;
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].active && clients[j].user_id == request_user_id) {
                            target = j;
                            break;
                        }
                    }
                    printf("Sending %s's profile to %s\n", clients[i].username, clients[target].username);
                    send_payload(out, buffer, sizeof(buffer), clients[target].fd);
                    /*
                    send(clients[target].fd, &out, sizeof(out), 0);
                    send(clients[target].fd, buffer, sizeof(buffer), 0);
                     */
                    break;
                }
                case WATCH_GAME: {
                    CallType out = WATCH_GAME;
                    int game_id;
                    if (recv(clients[i].fd, &game_id, sizeof (int), 0) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                    }
                    printf("Client %d wants to watch game %d\n", clients[i].user_id, game_id);
                    // then fetch players in the game and asks them to allow or not
                    //send(clients[i].fd, &out, sizeof(out), 0);
                    break;
                }
                default: break;
            }
        }
    }
    // TODO : nettoyer proprement l'espace mémoire quand le serveur s'arrête

    return 0;
}
