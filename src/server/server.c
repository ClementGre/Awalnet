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
} Client;

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

GameInstance *alloc_game() {
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

/*void send_your_turn(int fd) {
    CallType ct = YOUR_TURN;
    if (send(fd, &ct, sizeof(ct), 0) <= 0) perror("send YOUR_TURN");
}*/

/*void forward_move(int dest_fd, Move *m) {
    CallType ct = PLAY_MADE;
    if (send(dest_fd, &ct, sizeof(ct), 0) <= 0) perror("send PLAY_MADE");
    if (send(dest_fd, m, sizeof(Move), 0) <= 0) perror("send Move");
}
 */

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
        n = send(current_player.fd, &your_turn, sizeof(your_turn), 0);
        // the first player will receive the number - 1 indicating that he is the first to play, the second will receive any positive number
        send(current_player.fd, &move_made, sizeof(move_made), 0);
        printf("Sent YOUR_TURN to player fd %d for game %d\n", current_player.fd, g->game_id);
        if (n <= 0) {
            // client disconnected -> end game, notify opponent
            printf("Player fd %d disconnected, ending game %d\n", current_player.fd, g->game_id);
            CallType go = GAME_OVER;
            int opponent_fd = (tours % 2 == 0) ? g->game->player2.fd : g->game->player1.fd;
            send(opponent_fd, &go, sizeof(go), 0);
            break;
        }

        // THEN WAIT FOR A MOVE FROM THAT PLAYER
        CallType incoming;
        n = recv(current_player.fd, &incoming, sizeof(incoming), 0);
        if (n <= 0) {
            // client disconnected -> end game, notify opponent
            printf("Player fd %d disconnected, ending game %d\n", current_player.fd, g->game_id);
            CallType go = GAME_OVER;
            send(g->game->player2.fd, &go, sizeof(go), 0);
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
            int position_of_last_put_seed = moveSeeds(g->game, move_made  - 1);
            printf("stopped on position :%d\n", position_of_last_put_seed);

            g->game->player1.score += collectSeedsAndCountPoints(g->game, position_of_last_put_seed, 1);
        }
        else {
            int position_of_last_put_seed = moveSeeds(g->game, move_made + 5);
            printf("stopped on position :%d\n", position_of_last_put_seed);
            g->game->player2.score += collectSeedsAndCountPoints(g->game, position_of_last_put_seed, 2);
        }

        // then checks for win conditions
        if (g->game->player1.score == WINNING_SCORE || playerSeedsLeft(g->game, 2) < 6) {
            printf("\n---------------- PLAYER 1 WON !!! --------------\n");
            int win = 1;
            int lose = 0;
            CallType go = GAME_OVER;
            send(g->game->player1.fd, &go, sizeof(go), 0);
            send(g->game->player1.fd, &win, sizeof(int), 0);
            send(g->game->player2.fd, &go, sizeof(go), 0);
            send(g->game->player2.fd, &lose, sizeof(int), 0);
            break;
        }
        if (g->game->player2.score == WINNING_SCORE || playerSeedsLeft(g->game, 1) < 6) {
            printf("\n---------------- PLAYER 2 WON !!! --------------\n");
            int win = 1;
            int lose = 0;
            CallType go = GAME_OVER;
            send(g->game->player1.fd, &go, sizeof(go), 0);
            send(g->game->player1.fd, &lose, sizeof(int), 0);
            send(g->game->player2.fd, &go, sizeof(go), 0);
            send(g->game->player2.fd, &win, sizeof(int), 0);
            break;
        }

        tours++;

    }

    return NULL;
}

// ---------------------- GAME LOGIC ---------------------- //


int start_server() {
    printf("🚀 Starting Awalnet server...\n");
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);


    Client clients[MAX_CLIENTS];
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
                if (!clients[i].active) { clients[i].fd = new_socket; clients[i].active = 1; break; }
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
                close(clients[i].fd);
                clients[i].active = 0;
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
                    send(clients[i].fd, user_buffer, sizeof(user_buffer), 0);
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
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
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
                        CallType out = CHALLENGE;
                        send(clients[target].fd, &out, sizeof(out), 0);
                        send(clients[target].fd, &clients[i].user_id, sizeof(int), 0);
                        send(clients[target].fd, clients[i].username, USERNAME_SIZE + 1, 0);
                        printf("Challenge initialized by de %s(id=%d) to %s(id=%d) | socket %d to bind\n",
                               clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id, clients[target].fd);
                    } else {
                        printf("Utilisateur %d introuvable pour challenge.\n", opponent_user_id);
                        char error_msg[] = "User not found or not online.";
                        int previous_call = CHALLENGE;
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
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
                        CallType out = CHALLENGE_REQUEST_ANSWER;
                        send(clients[target].fd, &out, sizeof(out), 0);
                        send(clients[target].fd, &clients[i].user_id, sizeof(int), 0);
                        send(clients[target].fd, &answer, sizeof(int), 0);
                        if (answer == 1) {
                            printf("Challenge accepted by %s(id=%d) to %s(id=%d) | socket %d to bind\n",
                                   clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id, clients[target].fd);
                            CallType out = CHALLENGE_START;
                            // notify both clients that the challenge is starting now
                            send(clients[i].fd, &out, sizeof(out), 0);
                            send(clients[i].fd, &clients[target].username, sizeof(clients[target].username), 0);
                            send(clients[target].fd, &out, sizeof(out), 0);
                            send(clients[target].fd, &clients[i].username, sizeof(clients[i].username), 0);

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
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
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
                            strcat(user_list_buffer, " (id=");
                            char id_str[12];
                            sprintf(id_str, "%d", clients[u].user_id);
                            strcat(user_list_buffer, id_str);
                            strcat(user_list_buffer, ")");
                            strcat(user_list_buffer, "\n");
                            client_count++;
                        }
                    }
                    printf("Sending user list to %s (id=%d)\n", clients[i].username, clients[i].user_id);
                    send(clients[i].fd, &out, sizeof(out), 0);
                    if (client_count == 0) {
                        strcat(user_list_buffer, "No other users online.\n");
                    }
                    send(clients[i].fd, user_list_buffer, strlen(user_list_buffer) + 1, 0);
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
                        printf("User %s (id=%d) attempted to request their own profile. Ignored.\n",
                               clients[i].username, clients[i].user_id);
                        char error_msg[] = "To view your own profile, press 1.";
                        int previous_call = CONSULT_USER_PROFILE;
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
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
                        send(clients[target].fd, &out, sizeof(out), 0);
                        send(clients[target].fd, &clients[i].user_id, sizeof(int), 0);
                        printf("Request profile initialized by de %s(id=%d) to %s(id=%d) | socket %d to bind\n",
                               clients[i].username, clients[i].user_id, clients[target].username, clients[target].user_id, clients[target].fd);
                    } else {
                        printf("Utilisateur %d does not exist -> cannot send his profile\n", requested_user_id);
                        char error_msg[] = "User not found or not online.";
                        int previous_call = CONSULT_USER_PROFILE;
                        send(clients[i].fd, &error, sizeof(error), 0);
                        send(clients[i].fd, &previous_call, sizeof (previous_call), 0);
                        send(clients[i].fd, error_msg, sizeof(error_msg), 0);
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
                    send(clients[target].fd, &out, sizeof(out), 0);
                    send(clients[target].fd, buffer, sizeof(buffer), 0);
                    break;
                }
                default: break;
            }
        }
    }
    return 0;
}