#pragma once
#include <stdlib.h>
#include <string.h>
#include "model.h"
#include <stdint.h>

typedef enum GAME_OVER_REASON {
    WIN = 1,
    LOSE = 2,
    DRAW = 3,
    OPPONENT_DISCONNECTED = 4
} GAME_OVER_REASON;

#define MAX_CHAT_MESSAGE_SIZE 256

typedef enum CallType {
    CONNECT = 0, // Request Username
    LIST_USERS = 1,
    LIST_GAMES = 2,
    CONNECT_CONFIRM = 3,
    CHALLENGE = 4, // Request opponent_user_id
    CONSULT_USER_PROFILE = 5, // Request user_id
    ERROR = 6, // Notify client that an error occurred (for now message to explain why a challenge failed)
    SUCCESS = 7, // Notify client that the previous call was successful (for now used after a challenge is accepted)
    SENT_USER_PROFILE = 8, // Answer to CONSULT_USER_PROFILE -> send the user profile to the server
    RECEIVE_USER_PROFILE = 9, // Answer to sent_user_profile
    CHALLENGE_REQUEST_ANSWER = 10, // Answer to challenge to inform wether or not we accept the challenge request
    CHALLENGE_START = 11, // Notify both clients that the challenge is starting now
    PLAY_MADE = 12, // Notify the server that a play has been made, followed by
    YOUR_TURN = 13, // Notify the client that it's their turn to play
    GAME_OVER = 14, // Notify the client that the game is over (followed by GAME_OVER_REASON)
    LIST_ONGOING_GAMES = 15, // Notify the client with the list of ongoing games
    WATCH_GAME = 16, // Request ongoing game to watch
    ALLOW_CLIENT_TO_WATCH = 17, // Both players in a game have to answer that call when somebody wants to watch their game
    SEND_LOBBY_CHAT = 18, // Send chat message to all users in lobby (not in game)
    SEND_GAME_CHAT = 19, // Send chat message to opponent in game
    RECEIVE_LOBBY_CHAT = 20, // Receive lobby chat message (sender_id + username + message)
    RECEIVE_GAME_CHAT = 21, // Receive game chat message (sender_id + username + message)
    DOES_USER_EXIST = 22, // Server response to DOES_USER_EXIST to inform whether or not the user exists
    USER_WANTS_TO_WATCH = 23, // Notify both players that a user wants to watch their game
    ALLOW_WATCHER = 24, // Notify the watcher that he is allowed (or not) to watch the game
    WATCH_GAME_ANSWER = 25, // Answer to WATCH_GAME to inform wether or not we accept the watch request
    PLAY_MADE_WATCHER = 26, // Sends to the watchers the play made
    USER_WANTS_TO_EXIT_WATCH = 27, // Notify server that the watcher wants to stop watching the game
    GAME_OVER_WATCHER = 28 // Notify the watchers that the game is over


} CallType;

// Returns the size of a CallType payload, excluding the CallType itself.
size_t sizeof_CallType(CallType type);

// Returns true if the CallType is made to send from the client to the server
uint8_t is_server_CallType(CallType type);

// Returns true if the CallType is made to be sent from the server to the client, and processed in an async way
uint8_t is_client_async_CallType(CallType type);

// Returns true if the CallType is made to be sent from the server to the client, and processed as an interruption
uint8_t is_client_sync_CallType(CallType type);

// Maybe we could use those error codes to be more specific about what went wrong during a challenge request
typedef enum ERROR_CODE {
    USER_NOT_FOUND = 1,
    USER_OFFLINE = 2,
    CANNOT_CHALLENGE_YOURSELF = 3,
    ALREADY_IN_GAME = 4,
    UNKNOWN_ERROR = 99
} ERROR_CODE;


// Serialize User struct into a byte buffer
void serialize_User(User *user, uint8_t *buffer);

// Deserialize a byte buffer into User struct
void deserialize_User(uint8_t *buffer, User *user);
