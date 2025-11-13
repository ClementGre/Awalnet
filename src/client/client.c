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
#include "cligui.h"
#include "../common/api.h"
#include "../common/model.h"
#include "../common/utils.h"

// Global client state
static int client_fd = -1;
static pthread_t network_thread;

// Notification pipe: [0] = read, [1] = write
static int notification_pipe[2] = {-1, -1};

// Network message handling
static int incoming_available = 0;
static CallType incoming_call_type;
static uint8_t *incoming_payload;
static pthread_mutex_t incoming_lock = PTHREAD_MUTEX_INITIALIZER;

static void network_error(void) {
    printf("Erreur: Connexion au serveur perdue.\n");
    exit(EXIT_FAILURE);
}

void *listen_server(void *arg);
void process_sync_call(CallType type, int payload_size, uint8_t* payload);

/*
 * Initialize the connexion and start the network thread
 */
void client_init(const char *server_ip, int port) {
    struct sockaddr_in serv_addr;

    // Create notification pipe
    if (pipe(notification_pipe) < 0) {
        perror("pipe failed");
        exit(EXIT_FAILURE);
    }

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }

    if (connect(client_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    // Start network listener thread
    int *fd_arg = malloc(sizeof(int));
    *fd_arg = client_fd;
    if (pthread_create(&network_thread, NULL, listen_server, fd_arg) != 0) {
        perror("pthread_create network thread");
        free(fd_arg);
        exit(EXIT_FAILURE);
    }
}

int client_get_notification_fd(void) {
    return notification_pipe[0];
}

/*
 * This function listens to incoming server messages.
 * It should be started from a separate thread, and will trigger cligui functions
 */
void *listen_server(void *arg) {
    int fd = *(int *) arg;
    free(arg);

    CallType call_type;
    while (1) {
        // Read CallType
        size_t n = recv(fd, &call_type, sizeof(CallType), 0);
        if (n <= 0) network_error();

        int is_sync = is_client_sync_CallType(call_type);
        int is_async = is_client_async_CallType(call_type);
        if (!is_sync && !is_async) {
            printf("Avertissement: Réception d'un CallType invalide : %d\n", call_type);
            continue;
        }

        // Read Payload size
        uint32_t payload_size;
        n = recv(fd, &payload_size, sizeof(uint32_t), 0);
        if (n <= 0) network_error();

        // Read Payload
        uint8_t *payload = malloc(sizeof(uint8_t) * payload_size);
        n = recv(fd, payload, payload_size, 0);
        if (n <= 0) network_error();

        // Print
        //printf("Receiving CallType %d with payload size %d: ", call_type, payload_size);
        //for (int i = 0; i < payload_size; i++) printf("%02x", payload[i]);
        //printf("\n");

        if (is_async) {
            // Save data
            pthread_mutex_lock(&incoming_lock);
            // Wait until the last payload has been processed
            while (incoming_available) {
                pthread_mutex_unlock(&incoming_lock);
                usleep(10000);
                pthread_mutex_lock(&incoming_lock);
            }
            incoming_available = 1;
            incoming_call_type = call_type;
            incoming_payload = payload;
            pthread_mutex_unlock(&incoming_lock);

            // Notify UI thread via pipe
            char notify = 1;
            write(notification_pipe[1], &notify, 1);
        } else {
            process_sync_call(call_type, payload_size, payload);
        }
    }
}

/*
 * This function calls gui functions from messages received asynchronously.
 * It should be called regularly by the gui to process pending calls.
 */
int process_network_messages(void) {
    pthread_mutex_lock(&incoming_lock);

    if (!incoming_available) {
        pthread_mutex_unlock(&incoming_lock);
        return 0;
    }

    // Clear notification pipe
    char dummy;
    read(notification_pipe[0], &dummy, 1);

    if (incoming_call_type == CONNECT_CONFIRM) {
        User user;
        deserialize_User(incoming_payload, &user);
        on_connected(user);

    }else if (incoming_call_type == CHALLENGE) {
        // read values from the incoming_payload, taking the four first bytes and merging them
        int challenger_id = incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (incoming_payload[3] << 24);

        // Read username 32 bits
        char challenger_username[USERNAME_SIZE + 1];
        for (int index = 0; index <= USERNAME_SIZE; index++) {
            challenger_username[index] = incoming_payload[4 + index];
        }
        on_challenge_received(challenger_id, challenger_username);

    } else if (incoming_call_type == CHALLENGE_REQUEST_ANSWER) {
        // response to a sent challenge
        int challenged_user_id = read_int32_le(incoming_payload, 0);
        int answer = read_int32_le(incoming_payload, 4);

        on_challenge_request_answer(challenged_user_id, answer);

    } else if (incoming_call_type == ERROR) {
        // an error occurred in the previous call
        int previous_call = read_int32_le(incoming_payload, 0);
        char error_msg[256] = {0};
        memcpy(error_msg, incoming_payload + 4, 128);

        on_error(previous_call, error_msg);
    } else if (incoming_call_type == SUCCESS) {
        // confirmation of a successful previous call
        on_success();

    } else if (incoming_call_type == WATCH_GAME_ANSWER){
        int answer = incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (incoming_payload[3] << 24);
        on_watch_game_answer(answer);

    } else if (incoming_call_type == LIST_USERS) {
        // receiving the list of online users
        char user_list_buffer[1024] = {0};
        memcpy(user_list_buffer, incoming_payload, 1024);
        on_list_users(user_list_buffer);

    } else if (incoming_call_type == LIST_ONGOING_GAMES) {
        // receiving the list of ongoing games
        char games_list_buffer[1024] = {0};
        memcpy(games_list_buffer, incoming_payload, 1024);
        on_list_ongoing_games(games_list_buffer);

    } else if (incoming_call_type == RECEIVE_USER_PROFILE) {
        // receiving a user profile we requested
        uint8_t buffer[1024] = {0};
        memcpy(buffer, incoming_payload, 1024);
        on_receive_user_profile(buffer);

    } else if (incoming_call_type == DOES_USER_EXIST) {
        int does_exist = incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (incoming_payload[3] << 24);
        on_does_user_exist(does_exist);

    } else if (incoming_call_type == CHALLENGE_START) {
        // the challenge has started
        char opponent_username[USERNAME_SIZE + 1] = {0};
        memcpy(opponent_username, incoming_payload, USERNAME_SIZE + 1);
        on_challenge_start(opponent_username);

    } else if (incoming_call_type == YOUR_TURN) {
        // it's our turn to play
        int move_played = incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (incoming_payload[3] << 24);
        on_your_turn(move_played);

    } else if (incoming_call_type == GAME_OVER) {
        // the server notifies that the game is over
        GAME_OVER_REASON reason = (GAME_OVER_REASON) (incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (
                                                          incoming_payload[3] << 24));
        on_game_over(reason);

    } else if (incoming_call_type == RECEIVE_LOBBY_CHAT) {
        // receiving a lobby chat message
        int sender_id = incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (incoming_payload[3] << 24);
        char sender_username[USERNAME_SIZE + 1] = {0};
        memcpy(sender_username, incoming_payload + 4, USERNAME_SIZE + 1);
        char message[MAX_CHAT_MESSAGE_SIZE] = {0};
        memcpy(message, incoming_payload + 4 + USERNAME_SIZE + 1, MAX_CHAT_MESSAGE_SIZE);

        on_receive_lobby_chat(sender_id, sender_username, message);

    } else if (incoming_call_type == RECEIVE_GAME_CHAT) {
        // receiving a game chat message
        int sender_id = incoming_payload[0] + (incoming_payload[1] << 8) + (incoming_payload[2] << 16) + (incoming_payload[3] << 24);
        char sender_username[USERNAME_SIZE + 1] = {0};
        memcpy(sender_username, incoming_payload + 4, USERNAME_SIZE + 1);
        char message[MAX_CHAT_MESSAGE_SIZE] = {0};
        memcpy(message, incoming_payload + 4 + USERNAME_SIZE + 1, MAX_CHAT_MESSAGE_SIZE);

        on_receive_game_chat(sender_id, sender_username, message);

    } else {
        printf("\n>>> Message inconnu reçu du serveur.\n");
    }

    incoming_available = 0;
    free(incoming_payload);
    pthread_mutex_unlock(&incoming_lock);

    return 1;
}

void process_sync_call(CallType type, int payload_size, uint8_t* payload) {
    if (type == CONSULT_USER_PROFILE) {
        if (payload_size != sizeof(int)) {
            printf("Received invalid payload size %d for CallType %d", payload_size, type);
            return;
        }
        // we need to sent our user profile to the server because somebody requested it
        int request_user_id = payload[0] + (payload[1] << 8) + (payload[2] << 16) + (payload[3] << 24);
        interrupt_consult_user_profile(request_user_id);
    }else if (type == USER_WANTS_TO_WATCH) {
        int user_id = payload[0] + (payload[1] << 8) + (payload[2] << 16) + (payload[3] << 24);
        interrupt_user_wants_to_watch(user_id);
    }
}



/*
 * Send functions
 */

void send_connect(const char *username) {
    CallType call_type = CONNECT;
    if (send(client_fd, &call_type, sizeof(CallType), 0) <= 0) {
        perror("send failed");
        exit(EXIT_FAILURE);
    }
    if (send(client_fd, username, USERNAME_SIZE + 1, 0) <= 0) {
        perror("send failed");
        exit(EXIT_FAILURE);
    }
}

void send_list_users(void) {
    CallType ct = LIST_USERS;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
}

void send_challenge(int opponent_id) {
    CallType ct = CHALLENGE;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
    if (send(client_fd, &opponent_id, sizeof(int), 0) <= 0) perror("send failed");
}

void send_consult_user_profile(int user_id) {
    CallType ct = CONSULT_USER_PROFILE;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
    if (send(client_fd, &user_id, sizeof(int), 0) <= 0) perror("send failed");
}

void send_list_ongoing_games(void) {
    CallType ct = LIST_ONGOING_GAMES;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send failed");
}

void send_challenge_answer(int challenger_id, int answer) {
    CallType ct = CHALLENGE_REQUEST_ANSWER;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, &challenger_id, sizeof(challenger_id), 0) <= 0) perror("send id");
    if (send(client_fd, &answer, sizeof(answer), 0) <= 0) perror("send answer");
}

void send_user_profile(int request_user_id, uint8_t user_buffer[1024]) {
    CallType ct = SENT_USER_PROFILE;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, &request_user_id, sizeof(int), 0) <= 0) perror("send id");
    if (send(client_fd, user_buffer, sizeof(uint8_t) * 1024, 0) <= 0) perror("send user");
}

void send_play_made(int move) {
    CallType ct = PLAY_MADE;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, &move, sizeof(move), 0) <= 0) perror("send move");
}

void send_lobby_chat(const char* message) {
    CallType ct = SEND_LOBBY_CHAT;
    char msg_buffer[MAX_CHAT_MESSAGE_SIZE] = {0};
    strncpy(msg_buffer, message, MAX_CHAT_MESSAGE_SIZE - 1);

    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, msg_buffer, MAX_CHAT_MESSAGE_SIZE, 0) <= 0) perror("send message");
}

void send_game_chat(const char* message) {
    CallType ct = SEND_GAME_CHAT;
    char msg_buffer[MAX_CHAT_MESSAGE_SIZE] = {0};
    strncpy(msg_buffer, message, MAX_CHAT_MESSAGE_SIZE - 1);

    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, msg_buffer, MAX_CHAT_MESSAGE_SIZE, 0) <= 0) perror("send message");
}

void send_does_user_exist(int user_id) {
    CallType ct = DOES_USER_EXIST;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, &user_id, sizeof(int), 0) <= 0) perror("send response");
}

void send_game_watch_request(int game_id) {
    CallType ct = WATCH_GAME;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, &game_id, sizeof(int), 0) <= 0) perror("send game_id");
    printf("Votre demande pour regarder la partie %d a été envoyée au serveur.\n", game_id);
}

void send_game_watch_answer(int watcher_user_id, int answer){
    CallType ct = ALLOW_WATCHER;
    if (send(client_fd, &ct, sizeof(ct), 0) <= 0) perror("send ct");
    if (send(client_fd, &watcher_user_id, sizeof(int), 0) <= 0) perror("send watcher_user_id");
    if (send(client_fd, &answer, sizeof(int), 0) <= 0) perror("send answer");
}

