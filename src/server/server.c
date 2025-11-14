#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include "../common/api.h"
#include "../common/utils.h"

#define PORT 8080
#define MAX_CLIENTS 10
#define USERNAME_SIZE 32
#define MAX_GAMES 20


//#########   RANKINGG ##############
typedef struct {
    int id;
    int wins;
} PlayerRanking;

typedef struct {
    Player *data;
    int size;
    int capacity;
} MaxHeap;

MaxHeap *create_heap(int capacity) {
    MaxHeap *h = malloc(sizeof(MaxHeap));
    h->data = malloc(capacity * sizeof(Player));
    h->size = 0;
    h->capacity = capacity;
    return h;
}

void swap(PlayerRanking *a, PlayerRanking *b) {
    PlayerRanking tmp = *a;
    *a = *b;
    *b = tmp;
}

void heapify_up(MaxHeap *h, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->data[i].wins <= h->data[parent].wins) break;
        swap(&h->data[i], &h->data[parent]);
        i = parent;
    }
}

void heapify_down(MaxHeap *h, int i) {
    while (1) {
        int left = i * 2 + 1;
        int right = i * 2 + 2;
        int largest = i;

        if (left < h->size && h->data[left].wins > h->data[largest].wins)
            largest = left;
        if (right < h->size && h->data[right].wins > h->data[largest].wins)
            largest = right;

        if (largest == i) break;

        swap(&h->data[i], &h->data[largest]);
        i = largest;
    }
}

void heap_insert(MaxHeap *h, PlayerRanking p) {
    if (h->size == h->capacity) return;
    h->data[h->size] = p;
    heapify_up(h, h->size);
    h->size++;
}

void heap_update_wins(MaxHeap *h, int player_id) {
    for (int i = 0; i < h->size; i++) {
        if (h->data[i].id == player_id) {
            h->data[i].wins++;
            heapify_up(h, i);
            heapify_down(h, i);
            return;
        }
    }
}

Player heap_extract_max(MaxHeap *h) {
    Player max = h->data[0];
    h->data[0] = h->data[h->size - 1];
    h->size--;
    heapify_down(h, 0);
    return max;
}

char *print_ranking(MaxHeap *h) {
    int rank = 1;
    char buffer[1024] = {0};
    while (h->size > 0) {
        Player p = heap_extract_max(h);
        char line[64];
        line = snprintf(line, sizeof(line), "%der - joueur id %d - victoires : %d\n", rank, p.id, p.wins);
        // we add the line to the buffer
        strcat(buffer, line);
        rank++;
    }
    return strdup(buffer);
}

//##################

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

size_t send_payload(CallType calltype, uint8_t *payload, size_t payload_size, int fd) {
    send(fd, &calltype, sizeof(CallType), 0);
    send(fd, &payload_size, sizeof(uint32_t), 0);
    return send(fd, payload, payload_size, 0);
}

size_t send_error(CallType calltype, const char *error_msg, int fd) {
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
    int *watchers_fd;
    int *watchers_user_id;
    int num_watchers;
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
            g->watchers_fd = malloc((MAX_CLIENTS - 2) * sizeof(int)); // max watchers is total clients minus 2 players
            g->watchers_user_id = malloc((MAX_CLIENTS - 2) * sizeof(int)); // max watchers is total clients minus 2 players
            g->num_watchers = 0;
            games[i] = g;
            return g;
        }
    }
    return NULL;
}

void free_game(GameInstance *g) {
    if (!g) {
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


// ranking heap
MaxHeap *h = NULL;

void *game_thread(void *arg) {
    GameInstance *g = arg;

    uint8_t tours = 0;
    size_t n;
    int move_made = -1;
    while (g->running && tours <= MAX_ROUNDS) {
        Player current_player = (tours % 2 == 0) ? g->game->player1 : g->game->player2;
        uint8_t *payload = malloc(sizeof(uint32_t));
        write_int32_le(payload, 0, move_made);
        n = send_payload(YOUR_TURN, payload, sizeof(move_made), current_player.fd);
        printf("Sent YOUR_TURN to player fd %d for game %d\n", current_player.fd, g->game_id);
        // also send to watchers
        for (int w = 0; w < g->num_watchers; ++w) {
            int watcher_fd = g->watchers_fd[w];
            n = send_payload(PLAY_MADE_WATCHER, payload, sizeof(move_made), watcher_fd);
            printf("Sent PLAY_MADE_WATCHER to watcher fd %d for game %d\n", watcher_fd, g->game_id);
        }

        free(payload);

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

                // notify watchers
                for (int w = 0; w < g->num_watchers; ++w) {
                    int watcher_fd = g->watchers_fd[w];
                    send_payload(GAME_OVER_WATCHER, &gameOverReason, sizeof(gameOverReason), watcher_fd);
                    printf("Sent GAME_OVER_WATCHER to watcher fd %d for game %d\n", watcher_fd, g->game_id);
                }
            }
            break;
        }

        if (incoming == SEND_GAME_CHAT) {
            // Handle chat message
            char message[MAX_CHAT_MESSAGE_SIZE] = {0};
            n = recv(current_player.fd, message, MAX_CHAT_MESSAGE_SIZE, 0);
            if (n <= 0) {
                perror("recv chat message");
                break;
            }

            printf("Game %d chat from player fd %d: %s\n", g->game_id, current_player.fd, message);

            // Forward to opponent
            Player opponent = (tours % 2 == 0) ? g->game->player2 : g->game->player1;
            int sender_id = current_player.user_id;

            // Find sender username
            char sender_username[USERNAME_SIZE + 1] = {0};
            int sender_idx = find_client_index_by_fd(current_player.fd);
            if (sender_idx != -1) {
                pthread_mutex_lock(&clients_mutex);
                strncpy(sender_username, clients[sender_idx].username, USERNAME_SIZE);
                pthread_mutex_unlock(&clients_mutex);
            }

            CallType out = RECEIVE_GAME_CHAT;
            uint8_t buffer[sizeof(int) + USERNAME_SIZE + 1 + MAX_CHAT_MESSAGE_SIZE];
            memcpy(buffer, &sender_id, sizeof(int));
            memcpy(buffer + sizeof(int), sender_username, USERNAME_SIZE + 1);
            memcpy(buffer + sizeof(int) + USERNAME_SIZE + 1, message, MAX_CHAT_MESSAGE_SIZE);

            send_payload(out, buffer, sizeof(buffer), opponent.fd);

            // Wait for PLAY_MADE again (chat doesn't count as a turn)
            n = recv(current_player.fd, &incoming, sizeof(incoming), 0);
            if (n <= 0) {
                printf("Player fd %d disconnected after chat, ending game %d\n", current_player.fd, g->game_id);
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
                }
                break;
            }
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

        if (incoming == ALLOW_WATCHER) {
            int watcher_user_id;
            if (recv(current_player.fd, &watcher_user_id, sizeof(int), 0) <= 0) {
                perror("allow watcher autorisation");
                break;
            }
            // then fetch watcher fd
            int watcher_fd = -1;
            for (int j = 0; j < MAX_CLIENTS; j++) {
                if (clients[j].active && clients[j].user_id == watcher_user_id && !clients[j].in_game) {
                    watcher_fd = clients[j].fd;
                    break;
                }
            }
            if (watcher_fd == -1) {
                printf("Watcher %d not found or is in game\n", watcher_user_id);
                break;
            }
            // we need to find the game of the player who answered

            // then checks if he accepted or had previously been accepted by the other player
            for (int i = 0; i < g->num_watchers; i++) {
                if (g->watchers_fd[i] == watcher_user_id) {
                    printf("Watcher %d was already accepted to watch game %d\n", watcher_user_id, g->game_id);
                    break;
                }
            }
            // then sends him the answer
            int answer;
            if (recv(current_player.fd, &answer, sizeof(int), 0) <= 0) {
                perror("recv watcher answer");
                break;
            }
            printf("Game %d player fd %d answered %d to watcher %d\n", g->game_id, current_player.fd, answer, watcher_user_id);
            CallType out = WATCH_GAME_ANSWER;
            // we send him the answer
            send_payload(out, &answer, sizeof(int), watcher_fd);
            continue;
        }

        // then process the move to update to board and scores
        if (tours % 2 == 0) {
            // if the first player played
            int position_of_last_put_seed = moveSeeds(g->game, move_made - 1);
            g->game->player1.score += collectSeedsAndCountPoints(g->game, position_of_last_put_seed, 1);
            printf("SCORE : Player 1: %d | Player 2: %d  (game %d)\n", g->game->player1.score, g->game->player2.score, g->game_id);
        } else {
            // the second player made the last move
            int position_of_last_put_seed = moveSeeds(g->game, move_made + 5);
            g->game->player2.score += collectSeedsAndCountPoints(g->game, position_of_last_put_seed, 2);
            printf("SCORE : Player 1: %d | Player 2: %d  (game %d)\n", g->game->player1.score, g->game->player2.score, g->game_id);
        }

        // then checks for win conditions
        if (g->game->player1.score == WINNING_SCORE || playerSeedsLeft(g->game, 2) < 6) {
            printf("\n---------------- PLAYER 1 WON !!! --------------\n");
            CallType go = GAME_OVER;
            CallType goWatcher = GAME_OVER_WATCHER;
            GAME_OVER_REASON win = WIN;
            GAME_OVER_REASON lose = LOSE;
            send_payload(go, &win, sizeof(win), g->game->player1.fd);
            send_payload(go, &lose, sizeof(lose), g->game->player2.fd);
            // also notify watchers
            for (int w = 0; w < g->num_watchers; ++w) {
                int watcher_fd = g->watchers_fd[w];
                send_payload(goWatcher, &win, sizeof(win), watcher_fd);
                printf("Sent GAME_OVER_WATCHER to watcher fd %d for game %d\n", watcher_fd, g->game_id);
            }

            /*send(g->game->player1.fd, &go, sizeof(go), 0);
            send(g->game->player1.fd, &win, sizeof(win), 0);
            send(g->game->player2.fd, &go, sizeof(go), 0);
            send(g->game->player2.fd, &lose, sizeof(lose), 0);*/
            heap_update_wins(h, g->game->player1.user_id);

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
            CallType goWatcher = GAME_OVER_WATCHER;

            GAME_OVER_REASON win = WIN;
            GAME_OVER_REASON lose = LOSE;
            send_payload(go, &lose, sizeof(lose), g->game->player1.fd);
            send_payload(go, &win, sizeof(win), g->game->player2.fd);

            heap_update_wins(h, g->game->player2.user_id);


            // also notify watchers
            for (int w = 0; w < g->num_watchers; ++w) {
                int watcher_fd = g->watchers_fd[w];
                send_payload(goWatcher, &lose, sizeof(win), watcher_fd);
                printf("Sent GAME_OVER_WATCHER to watcher fd %d for game %d\n", watcher_fd, g->game_id);
            }
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
         *
        send(g->game->player1.fd, &go, sizeof(go), 0);
        send(g->game->player1.fd, &gameOverReason, sizeof(gameOverReason), 0);
        send(g->game->player2.fd, &go, sizeof(go), 0);
        send(g->game->player2.fd, &gameOverReason, sizeof(gameOverReason), 0);*/
        CallType goWatcher = GAME_OVER_WATCHER;
        // also notify watchers
        for (int w = 0; w < g->num_watchers; ++w
        ) {
            int watcher_fd = g->watchers_fd[w];
            send_payload(goWatcher, &gameOverReason, sizeof(gameOverReason), watcher_fd);
            printf("Sent GAME_OVER_WATCHER to watcher fd %d for game %d\n", watcher_fd, g->game_id);
        }
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

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
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
        } else if (activity == 0) {
            continue;
        }

        // new connection
        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen);
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

        h = create_heap(100); // create a heap with capacity 100 for ranking
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
                    heap_insert(h, (PlayerRanking ){clients[i].user_id, 0});

                    uint8_t user_buffer[1024] = {0};
                    serialize_User(&user, user_buffer);
                    CallType out = CONNECT_CONFIRM;
                    send_payload(out, user_buffer, sizeof(user_buffer), clients[i].fd);
                    break;
                }
                case CHALLENGE: {
                    CallType error = ERROR;
                    int opponent_user_id = 0;
                    if (read(clients[i].fd, &opponent_user_id, sizeof(int)) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    if (opponent_user_id == clients[i].user_id) {
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
                        printf("User %s (id=%d) has %d pending challenges.\n", clients[target].username, clients[target].user_id,
                               clients[target].nb_of_pending_challenges);
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

                    int answer = -1;
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
                            srand((unsigned int) time(NULL));
                            int starter = rand() % 2; // 0 or 1
                            Player player1, player2;
                            if (starter) {
                                player1 = newPlayer(clients[i].user_id, clients[i].fd);
                                player2 = newPlayer(clients[target].user_id, clients[target].fd);
                            } else {
                                player1 = newPlayer(clients[target].user_id, clients[target].fd);
                                player2 = newPlayer(clients[i].user_id, clients[i].fd);
                            }

                            // Create game instance
                            Game *game = newGame(&player1, &player2);

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


                case USER_WANTS_TO_EXIT_WATCH : {
                    // we need to delete the user from the game_id he is watching
                    int game_id = 0;
                    if (read(clients[i].fd, &game_id, sizeof(int)) <=
                        0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    GameInstance *g = games[game_id - 1];
                    if (g == NULL) {
                        printf("Game %d not found for watcher exit\n", game_id);
                        break;
                    }
                    // we need to remove the watcher from the watchers_fd array
                    int found = 0;
                    for (int w = 0; w < g->num_watchers; w++) {
                        if (g->watchers_fd[w] == clients[i].fd) {
                            // shift left
                            for (int k = w; k < g->num_watchers - 1; k++) {
                                g->watchers_fd[k] = g->watchers_fd[k + 1];
                            }
                            g->num_watchers--;
                            found = 1;
                            printf("Watcher %s (id=%d) exited watching game %d\n", clients[i].username,
                                   clients[i].user_id, game_id);
                            break;
                        }
                    }
                    if (!found) {
                        printf("Watcher %s (id=%d) was not found in watchers of game %d\n", clients[i].username,
                               clients[i].user_id, game_id);
                    }

                    break;
                }


                case CONSULT_USER_PROFILE: {
                    CallType out = CONSULT_USER_PROFILE;
                    int requested_user_id = 0;
                    int exists = 0;
                    if (read(clients[i].fd, &requested_user_id, sizeof(int)) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    if (requested_user_id == clients[i].user_id) {
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
                            exists = 1;
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

                case DOES_USER_EXIST: {
                    CallType out = DOES_USER_EXIST;
                    int requested_user_id = 0;
                    int exists = 0;
                    if (read(clients[i].fd, &requested_user_id, sizeof(int)) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    if (requested_user_id == clients[i].user_id) {
                        printf("User %s (id = %d) attempted to request add themselves as friend. Ignored.\n",
                               clients[i].username, clients[i].user_id);
                        char error_msg[] = "You cannot add yourself as friend";
                        int previous_call = DOES_USER_EXIST;

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
                            exists = 1;
                            break;
                        }
                    }
                    printf("User existence check for id=%d by %s(id=%d): %s\n", requested_user_id, clients[i].username, clients[i].user_id,
                           exists ? "EXISTS" : "DOES NOT EXIST");
                    send_payload(out, &exists, sizeof(int), clients[i].fd);
                    break;
                }
                case SENT_USER_PROFILE: {
                    CallType out = RECEIVE_USER_PROFILE;
                    int request_user_id;
                    if (recv(clients[i].fd, &request_user_id, sizeof(int), 0) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    // we get the user_profile serialized
                    uint8_t buffer[1024] = {0};
                    if (recv(clients[i].fd, buffer, sizeof(buffer), 0) <= 0) {
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
                    int game_id;
                    if (recv(clients[i].fd, &game_id, sizeof(int), 0) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }
                    // because games are displayed to users starting from 1
                    game_id--;
                    // first we need to check if the game exists
                    if (game_id < 0 || game_id >= next_game_id || games[game_id] == NULL) {
                        printf("Client %d requested to watch non-existing game %d\n", clients[i].user_id, game_id);
                        CallType error = ERROR;
                        char error_msg[] = "The requested game does not exist.";
                        send_error(call_type, error_msg, clients[i].fd);
                        break;
                    }
                    // then fetch players in the game and asks them to allow or not
                    //CallType user_request = USER_WANTS_TO_WATCH;
                    //send_payload(user_request, &clients[i].user_id, sizeof(int), games[game_id]->game->player1.fd);
                    //send_payload(user_request, &clients[i].user_id, sizeof(int), games[game_id]->game->player2.fd);
                    //printf("Client %d wants to watch game %d\n", clients[i].user_id, game_id);
                    // for now we do not handle multiple watchers or refusals, we just let the client watch directly
                    // so we need to store it in the game instance
                    games[game_id]->watchers_user_id[games[game_id]->num_watchers] = clients[i].user_id;
                    games[game_id]->watchers_fd[games[game_id]->num_watchers] = clients[i].fd;
                    games[game_id]->num_watchers++;
                    break;
                }
                case SEND_LOBBY_CHAT: {
                    char message[MAX_CHAT_MESSAGE_SIZE] = {0};
                    if (recv(clients[i].fd, message, MAX_CHAT_MESSAGE_SIZE, 0) <= 0) {
                        close(clients[i].fd);
                        clients[i].active = 0;
                        break;
                    }

                    printf("Lobby chat from %s (id=%d): %s\n", clients[i].username, clients[i].user_id, message);

                    // Broadcast to all clients not in game (except sender)
                    CallType out = RECEIVE_LOBBY_CHAT;
                    uint8_t buffer[sizeof(int) + USERNAME_SIZE + 1 + MAX_CHAT_MESSAGE_SIZE];
                    memcpy(buffer, &clients[i].user_id, sizeof(int));
                    memcpy(buffer + sizeof(int), clients[i].username, USERNAME_SIZE + 1);
                    memcpy(buffer + sizeof(int) + USERNAME_SIZE + 1, message, MAX_CHAT_MESSAGE_SIZE);

                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].active && clients[j].fd != clients[i].fd && !clients[j].in_game) {
                            send_payload(out, buffer, sizeof(buffer), clients[j].fd);
                        }
                    }
                    break;
                }
                default: break;
            }
        }
    }
    // TODO : nettoyer proprement l'espace mémoire quand le serveur s'arrête

    return 0;
}
