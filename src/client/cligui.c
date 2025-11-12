#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include "cligui.h"
#include <pthread.h>
#include "client.h"
#include "../common/api.h"
#include "../common/model.h"


static pthread_mutex_t user_lock = PTHREAD_MUTEX_INITIALIZER;
static User user;

// Input buffer state for preserving context across interruptions
typedef struct InputContext {
    char buffer[MAX_CHAT_MESSAGE_SIZE];
    char prompt[128];
    int active;
} InputContext;

static InputContext input_context = {0};

// Friend list management
typedef struct Friend {
    int user_id;
} Friend;

// UI State struct to reduce global variables
typedef struct UIState {
    // Challenge state
    int is_connected;
    int pending_challenge;
    int challenger_id;
    char challenger_username[USERNAME_SIZE + 1];
    int sent_challenges;

    // Game state
    int in_game;
    int your_turn;
    int last_move;
    GAME_OVER_REASON game_over_reason;
    int game_is_over;
    Game *game;
    Player me;
    Player opponent;
    int order;

    // Friends
    Friend friends[MAX_FRIEND];
    int nb_friends;

    // Flags for async responses
    int waiting_for_user_list;
    int waiting_for_game_list;
    int waiting_for_user_profile;
    int does_user_exist;
} UIState;

static UIState ui_state = {0};

void clear_terminal(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

/*
 * Save current input context before interruption
 */
static void save_input_context(const char* prompt) {
    strncpy(input_context.prompt, prompt, sizeof(input_context.prompt) - 1);
    input_context.active = 1;
}

/*
 * Restore input context after handling interruption
 */
static void restore_input_context(void) {
    if (input_context.active) {
        printf("%s", input_context.prompt);
        fflush(stdout);
    }
}

/*
 * Read a line from stdin with interruption support via select()
 * Returns: 1 if line read successfully, 0 if interrupted by network event, -1 on error
 * The prompt parameter is saved to allow re-display after interruptions
 */
static int read_line_interruptible_with_prompt(char *buffer, size_t size, const char* prompt) {
    int notification_fd = client_get_notification_fd();
    fd_set readfds;

    // Save context in case of interruption
    save_input_context(prompt);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(notification_fd, &readfds);

        int max_fd = (STDIN_FILENO > notification_fd) ? STDIN_FILENO : notification_fd;

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return -1;
        }

        // Check if network event occurred
        if (FD_ISSET(notification_fd, &readfds)) {
            // Process all pending messages
            while (process_network_messages()) {}

            // Re-display prompt after interruption
            printf("\n");
            restore_input_context();
            continue; // Go back to select, don't return
        }

        // Check if stdin is ready
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, size, stdin) == NULL) {
                clearerr(stdin);
                continue;
            }
            input_context.active = 0;
            return 1; // Successfully read line
        }
    }
}

/*
 * Compatibility wrapper for read_line_interruptible
 */
static int read_line_interruptible(char *buffer, size_t size) {
    return read_line_interruptible_with_prompt(buffer, size, "");
}

/*
 * Wait for network events or timeout
 * Returns: 1 if network event, 0 if timeout
 */
static int wait_for_network_event(int timeout_ms) {
    int notification_fd = client_get_notification_fd();
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(notification_fd, &readfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(notification_fd + 1, &readfds, NULL, NULL, &tv);

    if (ret < 0) {
        if (errno != EINTR) perror("select");
        return 0;
    }

    return ret > 0;
}

static void init_game_state(void) {
    pthread_mutex_lock(&user_lock);
    ui_state.me = newPlayer(user.id, -1);
    pthread_mutex_unlock(&user_lock);
    ui_state.opponent = newPlayer(-1, -1);
    ui_state.order = 0;
    ui_state.game = NULL;
    ui_state.your_turn = 0;
    ui_state.last_move = -1;
    ui_state.game_is_over = 0;
}

static void cleanup_game_state(void) {
    if (ui_state.game) {
        free(ui_state.game);
        ui_state.game = NULL;
    }
    ui_state.in_game = 0;
    ui_state.your_turn = 0;
    ui_state.last_move = -1;
    ui_state.game_is_over = 0;
    ui_state.order = 0;
}

static void handle_waiting_for_opponent(void) {
    if (ui_state.your_turn || !ui_state.in_game) {
        return;
    }

    // While waiting, allow chat
    printf("En attente de l'adversaire... (Tapez un message pour chatter ou appuyez sur Entrée pour attendre)\n> ");
    fflush(stdout);

    char input_buf[MAX_CHAT_MESSAGE_SIZE];
    int ret = read_line_interruptible(input_buf, sizeof(input_buf));

    if (ret == 0) {
        // Interrupted by network event (probably opponent played)
        process_network_messages();
        return;
    }

    if (ret > 0) {
        input_buf[strcspn(input_buf, "\n")] = 0;
        if (strlen(input_buf) > 0) {
            send_game_chat(input_buf);
            printf(">>> Message envoyé.\n");
        }
    }
}

static void handle_game_turn(void) {
    if (!ui_state.your_turn) {
        return;
    }

    // Check if game is over
    if (ui_state.game_is_over) {
        GAME_OVER_REASON reason = ui_state.game_over_reason;

        pthread_mutex_lock(&user_lock);
        if (reason == WIN) {
            printf("\n>>> Vous avez gagné la partie ! Félicitations !\n");
            user.total_wins++;
        }
        else if (reason == LOSE){
            printf("\n>>> Vous avez perdu la partie.\n");
        }
        else if (reason == DRAW) {
            printf("\n>>> La partie s'est terminée par un match nul.\n");
        }
        else if (reason == OPPONENT_DISCONNECTED) {
            printf("\n>>> Votre adversaire s'est déconnecté. Vous gagnez la partie par forfait !\n");
            user.total_wins++;
        }
        user.total_games++;
        pthread_mutex_unlock(&user_lock);

        cleanup_game_state();
        return;
    }

    // Initialize game if needed
    int move_played = ui_state.last_move;
    if (ui_state.game == NULL) {
        if (move_played == -1) {
            ui_state.game = newGame(&ui_state.me, &ui_state.opponent);
            ui_state.order = 1;
        } else {
            ui_state.game = newGame(&ui_state.opponent, &ui_state.me);
            ui_state.order = 2;
        }
        printf("\n>>> La partie commence ! Vous êtes le joueur %d.\n", ui_state.order);
    }

    // Apply opponent's move if present
    if (move_played != -1) {
        printf("Le dernier coup joué par votre adversaire était la position %d.\n", move_played);
        int opponent_order = (ui_state.order == 1) ? 2 : 1;
        int adjusted_position = (opponent_order == 2) ? move_played + 5 : move_played - 1;
        int position_of_last_put_seed = moveSeeds(ui_state.game, adjusted_position);
        int new_points = collectSeedsAndCountPoints(ui_state.game, position_of_last_put_seed, opponent_order);
        ui_state.opponent.score += new_points;
    } else {
        printf("Vous êtes le premier à jouer.\n");
    }

    printGame(ui_state.game, ui_state.order);
    printf("    SCORE : p1 : %d | p2 : %d\n", ui_state.me.score, ui_state.opponent.score);

    // Get player input
    char input_buf[32];
    int local_choice = -1;

    while (1) {
        const char* prompt = "ENTREZ UNE POSITION DE CASE POUR DÉPLACER LES GRAINES (1-6) ou un message pour chatter : ";
        printf("%s", prompt);
        fflush(stdout);

        int ret = read_line_interruptible_with_prompt(input_buf, sizeof(input_buf), prompt);
        if (ret < 0) continue;

        // Try to parse as number
        if (sscanf(input_buf, "%d", &local_choice) == 1) {
            if (local_choice >= 1 && local_choice <= 6) break;
            printf("Position invalide. Choisissez entre 1 et 6.\n");
        } else {
            // Not a number - treat as chat message
            input_buf[strcspn(input_buf, "\n")] = 0;
            if (strlen(input_buf) > 0) {
                send_game_chat(input_buf);
                printf(">>> Message envoyé.\n");
            }
        }
    }

    // Validate the choice has seeds
    int adjusted_position = (ui_state.order == 1) ? local_choice - 1 : local_choice + 5;
    while (ui_state.game->board[adjusted_position] == 0) {
        const char* prompt = "Pas de graines dans cette case. Choisissez une autre case (ou un message pour chatter) : ";
        printf("%s", prompt);
        fflush(stdout);

        int ret = read_line_interruptible_with_prompt(input_buf, sizeof(input_buf), prompt);
        if (ret < 0) continue;

        // Try to parse as number
        if (sscanf(input_buf, "%d", &local_choice) == 1) {
            adjusted_position = (ui_state.order == 1) ? local_choice - 1 : local_choice + 5;
        } else {
            // Not a number - treat as chat message
            input_buf[strcspn(input_buf, "\n")] = 0;
            if (strlen(input_buf) > 0) {
                send_game_chat(input_buf);
                printf(">>> Message envoyé.\n");
            }
        }
    }

    // Make the move
    int position_of_last_put_seed = moveSeeds(ui_state.game, adjusted_position);
    int new_points = collectSeedsAndCountPoints(ui_state.game, position_of_last_put_seed, ui_state.order);
    printf("Vous avez collecté %d points avec ce coup.\n", new_points);
    ui_state.me.score += new_points;

    // Send move to server
    send_play_made(local_choice);
    ui_state.your_turn = 0;

    printf("\n>>> Coup joué : position %d envoyée au serveur.\n", local_choice);
    printf("Nouveau board : \n");
    printGame(ui_state.game, ui_state.order);
    printf("    SCORE : p1 : %d | p2 : %d\n", ui_state.me.score, ui_state.opponent.score);
}

static void handle_pending_challenge(void) {
    if (!ui_state.pending_challenge) {
        return;
    }

    int answer = -1;
    while (answer != 0 && answer != 1) {
        printf("1 pour accepter, 0 pour refuser : ");
        fflush(stdout);

        char input_buf[32];
        int ret = read_line_interruptible(input_buf, sizeof(input_buf));
        if (ret == 0) {
            // Interrupted by network event - process it and continue asking
            process_network_messages();
            continue;
        }
        if (ret < 0) continue;

        if (sscanf(input_buf, "%d", &answer) != 1) continue;
    }

    send_challenge_answer(ui_state.challenger_id, answer);

    if (answer == 1) {
        printf(">>> Vous avez accepté le défi de %s !\n", ui_state.challenger_username);
        ui_state.in_game = 1;
        init_game_state();
    } else {
        printf(">>> Vous avez refusé le défi de %s.\n", ui_state.challenger_username);
    }

    ui_state.pending_challenge = 0;
}

// Handler functions called by client.c when messages arrive

void interrupt_consult_user_profile(int request_user_id) {
    uint8_t user_buffer[1024] = {0};
    pthread_mutex_lock(&user_lock);
    serialize_User(&user, user_buffer);
    pthread_mutex_unlock(&user_lock);
    send_user_profile(request_user_id, user_buffer);

}

void on_connected(User usr) {
    ui_state.is_connected = 1;
    pthread_mutex_lock(&user_lock);
    user = usr;
    printf("Connecté en tant que %s (id=%d)\n", user.username, user.id);
    pthread_mutex_unlock(&user_lock);
}

void on_challenge_received(int challenger_id, char challenger_username[USERNAME_SIZE + 1]) {
    printf("\n>>> Défi reçu de %s (id=%d)\n", challenger_username, challenger_id);
    printf("Accepter le défi ? (1 = Oui / 0 = Non) : ");

    ui_state.pending_challenge = 1;
    ui_state.challenger_id = challenger_id;
    strncpy(ui_state.challenger_username, challenger_username, USERNAME_SIZE);
    ui_state.challenger_username[USERNAME_SIZE] = '\0';
}

void on_challenge_request_answer(int challenged_user_id, int answer) {
    if (answer == 0) {
        printf("\n>>> Votre défi à l'utilisateur %d a été refusé.\n", challenged_user_id);
    } else {
        printf("\n>>> Votre défi à l'utilisateur %d a été accepté !\n", challenged_user_id);
    }
    ui_state.sent_challenges--;
}

void on_error(int previous_call, char error_msg[256]) {
    printf(">>> Erreur : %s\n", error_msg);
    fflush(stdout);

    switch (previous_call) {
        case CHALLENGE:
            ui_state.sent_challenges--;
            break;
        case CONSULT_USER_PROFILE:
            ui_state.waiting_for_user_profile = 0;
            ui_state.does_user_exist = 0;
            break;
        case LIST_USERS:
            ui_state.waiting_for_user_list = 0;
            break;
        case LIST_ONGOING_GAMES:
            ui_state.waiting_for_game_list = 0;
            break;
        default:
            break;
    }
}

void on_success(void) {
    printf(">>> Votre demande a été envoyée avec succès.\n");
}

void on_list_users(char user_list_buffer[1024]) {
    printf("Utilisateurs en ligne :\n%s\n", user_list_buffer);
    ui_state.waiting_for_user_list = 0;
}

void on_list_ongoing_games(char games_list_buffer[1024]) {
    printf("Parties en cours :\n%s\n", games_list_buffer);
    ui_state.waiting_for_game_list = 0;
}

void on_receive_user_profile(uint8_t buffer[1024]) {
    User user_received;
    deserialize_User(buffer, &user_received);
    printf("Profil de l'utilisateur demandé :\n");
    printUser(&user_received);
    ui_state.waiting_for_user_profile = 0;
    ui_state.does_user_exist = 1;

}

void on_challenge_start(char opponent_username[USERNAME_SIZE + 1]) {
    clear_terminal();
    printf("\n>>> Le défi avec %s commence maintenant !\n", opponent_username);

    ui_state.in_game = 1;
    init_game_state();
}

void on_your_turn(int move_played) {
    printf("\n>>> Le serveur a notifié que c'est votre tour de jouer.\n");
    ui_state.your_turn = 1;
    ui_state.last_move = move_played;
}

void on_game_over(GAME_OVER_REASON reason) {
    printf("\n>>> Le serveur a notifié que la partie est terminée.\n");
    ui_state.game_over_reason = reason;
    ui_state.your_turn = 1;
    ui_state.game_is_over = 1;
}

void on_receive_lobby_chat(int sender_id, char sender_username[USERNAME_SIZE + 1], char message[MAX_CHAT_MESSAGE_SIZE]) {
    printf("\n[LOBBY] %s: %s\n", sender_username, message);
    fflush(stdout);
}

void on_receive_game_chat(int sender_id, char sender_username[USERNAME_SIZE + 1], char message[MAX_CHAT_MESSAGE_SIZE]) {
    printf("\n[GAME] %s: %s\n", sender_username, message);
    fflush(stdout);
}

void run_client_ui(void) {
    // Get username with interruptible input
    char username[USERNAME_SIZE + 1];
    printf("Entrez votre nom d'utilisateur: ");
    fflush(stdout);

    char input_buf[USERNAME_SIZE + 2];
    int ret = read_line_interruptible(input_buf, sizeof(input_buf));
    if (ret == 1) {
        strncpy(username, input_buf, USERNAME_SIZE);
        username[strcspn(username, "\n")] = 0;
        username[USERNAME_SIZE] = '\0';
        send_connect(username);
    }

    while (1) {
        // Process any pending network messages
        while (process_network_messages()) {
            // Keep processing until queue is empty
        }

        if (!ui_state.is_connected) {
            // Wait for connection with timeout
            wait_for_network_event(100);
            continue;
        }

        // Handle game turn if in game and it's our turn
        if (ui_state.in_game && ui_state.your_turn) {
            handle_game_turn();
            continue;
        }

        // Handle waiting for opponent (allows chat while waiting)
        if (ui_state.in_game && !ui_state.your_turn) {
            handle_waiting_for_opponent();
            continue;
        }

        // Handle pending challenge
        if (ui_state.pending_challenge) {
            handle_pending_challenge();
            continue;
        }

        // Skip menu if in game or waiting for async response
        if (ui_state.in_game || ui_state.waiting_for_user_list ||
            ui_state.waiting_for_game_list || ui_state.waiting_for_user_profile) {
            wait_for_network_event(100);
            continue;
        }

        // Display menu
        printf("\n===== MENU =====\n");
        printf(" 1 - Voir votre profil\n");
        printf(" 2 - Afficher les utilisateurs en ligne\n");
        printf(" 3 - Défier un utilisateur\n");
        printf(" 4 - Consulter le profil d'un utilisateur\n");
        printf(" 5 - Afficher les parties en cours\n");
        printf(" 6 - Gérer mes amis\n");
        printf(" 7 - Définir ma bio\n");
        printf(" 8 - Regarder une partie en cours\n");
        printf(" 9 - Envoyer un message au lobby\n");
        printf(" 10 - Quitter\n");
        printf("Votre choix: ");
        fflush(stdout);

        int choice;
        char input_buf[32];
        int ret = read_line_interruptible(input_buf, sizeof(input_buf));
        if (ret == 0) {
            // Interrupted by network event - process and re-display menu
            continue;
        }
        if (ret < 0) continue;

        if (sscanf(input_buf, "%d", &choice) != 1) {
            printf("Entrée invalide.\n");
            continue;
        }

        switch (choice) {
            case 1:
                pthread_mutex_lock(&user_lock);
                printUser(&user);
                pthread_mutex_unlock(&user_lock);
                break;

            case 2:
                send_list_users();
                ui_state.waiting_for_user_list = 1;
                break;

            case 3:
                if (ui_state.sent_challenges) {
                    printf("Vous avez déjà envoyé une demande, veuillez attendre la réponse avant d'en faire de nouvelles\n");
                    break;
                }
                printf("Entrer l'id de l'adversaire: ");
                fflush(stdout);
                int opponent_id;
                ret = read_line_interruptible(input_buf, sizeof(input_buf));
                if (ret == 0) break; // Interrupted
                if (ret < 0 || sscanf(input_buf, "%d", &opponent_id) != 1) {
                    printf("Entrée invalide.\n");
                    break;
                }
                send_challenge(opponent_id);
                ui_state.sent_challenges++;
                printf(">>> En attente de la réponse de l'adversaire...\n");
                break;

            case 4:
                printf("Entrer l'id du joueur: ");
                fflush(stdout);
                int user_profile_id;
                ret = read_line_interruptible(input_buf, sizeof(input_buf));
                if (ret == 0) break; // Interrupted
                if (ret < 0 || sscanf(input_buf, "%d", &user_profile_id) != 1) {
                    printf("Entrée invalide.\n");
                    break;
                }
                send_consult_user_profile(user_profile_id);
                ui_state.waiting_for_user_profile = 1;
                break;

            case 5:
                send_list_ongoing_games();
                ui_state.waiting_for_game_list = 1;
                break;

            case 6:
                // Friends management
                printf("Liste de vos amis:\n");
                for (int i = 0; i < ui_state.nb_friends; i++) {
                    printf("(%d)\n", ui_state.friends[i].user_id);
                }

                if (ui_state.nb_friends == 0) {
                    printf("Vous n'avez aucun ami.\n");
                }

                printf("Que voulez-vous faire?\n");
                printf(" 1 - Ajouter un ami\n");
                printf(" 2 - Supprimer un ami\n");
                printf(" 3 - Quitter\n");
                printf("Votre choix: ");

                int friend_choice;
                ret = read_line_interruptible(input_buf, sizeof(input_buf));
                if (ret == 0) break; // Interrupted
                if (ret < 0 || sscanf(input_buf, "%d", &friend_choice) != 1) {
                    printf("Entrée invalide.\n");
                    break;
                }

                if (friend_choice == 1) {
                    printf("Entrer l'id du joueur: ");
                    fflush(stdout);
                    int friend_id;
                    ret = read_line_interruptible(input_buf, sizeof(input_buf));
                    if (ret == 0) break; // Interrupted
                    if (ret < 0 || sscanf(input_buf, "%d", &friend_id) != 1) {
                        printf("Entrée invalide.\n");
                        break;
                    }
                    send_consult_user_profile(friend_id);
                    ui_state.waiting_for_user_profile = 1;

                    // WAITS FOR THE ANSWER

                    if (ui_state.does_user_exist) {
                        // add it
                        //friends
                        ui_state.friends[ui_state.nb_friends].user_id = friend_id;
                        ui_state.nb_friends++;
                    }
                }

                else if (friend_choice == 2) {
                    if (ui_state.nb_friends == 0) {
                        printf("Vous n'avez aucun ami à supprimer\n");
                    }
                }
                break;


            case 7:
                printf("Ecrivez votre bio : \n");
                char *bio = malloc(sizeof(char) * (BIO_SIZE + 1));
                scanf("%[^\n]%*c", bio);
                pthread_mutex_lock(&user_lock);
                user.bio = bio;
                pthread_mutex_unlock(&user_lock);
                break;
            case 8:
                int game_id;
                printf("Entrer l'id de la game: ");
                fflush(stdout);
                ret = read_line_interruptible(input_buf, sizeof(input_buf));
                if (ret == 0) break; // Interrupted
                if (ret < 0 || sscanf(input_buf, "%d", &game_id) != 1) {
                    printf("Entrée invalide.\n");
                    break;
                }
                // then send the ID of the game to the server

                break;
            case 9:
                printf("Entrez votre message: ");
                fflush(stdout);
                char chat_msg[MAX_CHAT_MESSAGE_SIZE];
                ret = read_line_interruptible(chat_msg, sizeof(chat_msg));
                if (ret == 0) break; // Interrupted
                if (ret > 0) {
                    chat_msg[strcspn(chat_msg, "\n")] = 0;
                    if (strlen(chat_msg) > 0) {
                        send_lobby_chat(chat_msg);
                        printf(">>> Message envoyé au lobby.\n");
                    }
                }
                break;

            case 10:
                printf("Déconnecté.\n");
                exit(EXIT_SUCCESS);

            default:
                printf("Choix invalide.\n");
                break;
        }
    }
}
