#include "api.h"

// Returns true if the CallType is made to be sent from the client to the server
uint8_t is_server_CallType(CallType type) {
    switch (type) {
        case CONNECT: return 1;
        case CONNECT_CONFIRM: return 0;
        case CHALLENGE: return 1;
        case LIST_USERS: return 1;
        case LIST_GAMES: return 1;
        case CONSULT_USER_PROFILE: return 1;
        case ERROR: return 0;
        case SUCCESS: return 0;
        case SENT_USER_PROFILE: return 1;
        case RECEIVE_USER_PROFILE: return 0;
        case CHALLENGE_REQUEST_ANSWER : return 1;
        case CHALLENGE_START: return 0;
        case PLAY_MADE: return 1;
        case YOUR_TURN: return 0;
        case GAME_OVER: return 0;
        case LIST_ONGOING_GAMES: return 1;
        case WATCH_GAME: return 1;
        case SEND_LOBBY_CHAT: return 1;
        case SEND_GAME_CHAT: return 1;
        case RECEIVE_LOBBY_CHAT: return 0;
        case RECEIVE_GAME_CHAT: return 0;
        case DOES_USER_EXIST : return 1;
    }
    return 0;
}


// Returns true if the CallType is made to be sent from the server to the client, and processed in an async way
uint8_t is_client_async_CallType(CallType type) {
    switch (type) {
        case CONNECT: return 1;
        case CONNECT_CONFIRM: return 1;
        case CHALLENGE: return 1;
        case LIST_USERS: return 1;
        case LIST_GAMES: return 0;
        case CONSULT_USER_PROFILE: return 0;
        case ERROR: return 1;
        case SUCCESS: return 1;
        case SENT_USER_PROFILE: return 1;
        case RECEIVE_USER_PROFILE: return 1;
        case CHALLENGE_REQUEST_ANSWER : return 1;
        case CHALLENGE_START: return 1;
        case PLAY_MADE: return 0;
        case YOUR_TURN: return 1;
        case GAME_OVER: return 1;
        case LIST_ONGOING_GAMES: return 1;
        case WATCH_GAME: return 1;
        case SEND_LOBBY_CHAT: return 0;
        case SEND_GAME_CHAT: return 0;
        case RECEIVE_LOBBY_CHAT: return 1;
        case RECEIVE_GAME_CHAT: return 1;
        case DOES_USER_EXIST : return 1;
    }
    return 0;
}

// Returns true if the CallType is made to be sent from the server to the client, and processed as an interruption
uint8_t is_client_sync_CallType(CallType type) {
    switch (type) {
        case CONNECT: return 0;
        case CONNECT_CONFIRM: return 0;
        case CHALLENGE: return 0;
        case LIST_USERS: return 0;
        case LIST_GAMES: return 0;
        case CONSULT_USER_PROFILE: return 1;
        case ERROR: return 0;
        case SUCCESS: return 0;
        case SENT_USER_PROFILE: return 0;
        case RECEIVE_USER_PROFILE: return 0;
        case CHALLENGE_REQUEST_ANSWER : return 0;
        case CHALLENGE_START: return 0;
        case PLAY_MADE: return 0;
        case YOUR_TURN: return 0;
        case GAME_OVER: return 0;
        case LIST_ONGOING_GAMES: return 0;
        case WATCH_GAME: return 0;
        case SEND_LOBBY_CHAT: return 0;
        case SEND_GAME_CHAT: return 0;
        case RECEIVE_LOBBY_CHAT: return 0;
        case RECEIVE_GAME_CHAT: return 0;
        case DOES_USER_EXIST : return 0;
    }
    return 0;
}


void serialize_User(User *user, uint8_t *buffer) {
    memcpy(buffer, user->username, sizeof(user->username));
    memcpy(buffer + sizeof(user->username), &user->id, sizeof(user->id));
    // Serialize bio as a string preceded by its length
    int bio_len = strlen(user->bio);
    memcpy(buffer + sizeof(user->username) + sizeof(user->id), &bio_len, sizeof(bio_len));
    memcpy(buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len), user->bio, bio_len);
    memcpy(buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len) + bio_len, &user->total_score, sizeof(user->total_score));
    memcpy(buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len) + bio_len + sizeof(user->total_score), &user->total_games, sizeof(user->total_games));
    memcpy(buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len) + bio_len + sizeof(user->total_score) + sizeof(user->total_games), &user->total_wins, sizeof(user->total_wins));
}

// Deserialize a byte buffer into User struct
void deserialize_User(uint8_t *buffer, User *user) {
    memcpy(user->username, buffer, sizeof(user->username));
    memcpy(&user->id, buffer + sizeof(user->username), sizeof(user->id));
    int bio_len;
    memcpy(&bio_len, buffer + sizeof(user->username) + sizeof(user->id), sizeof(bio_len));
    user->bio = malloc(bio_len + 1);
    memcpy(user->bio, buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len), bio_len);
    user->bio[bio_len] = '\0';
    memcpy(&user->total_score, buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len) + bio_len, sizeof(user->total_score));
    memcpy(&user->total_games, buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len) + bio_len + sizeof(user->total_score), sizeof(user->total_games));
    memcpy(&user->total_wins, buffer + sizeof(user->username) + sizeof(user->id) + sizeof(bio_len) + bio_len + sizeof(user->total_score) + sizeof(user->total_games), sizeof(user->total_wins));
}
