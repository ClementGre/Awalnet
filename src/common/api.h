#pragma once
#include <stdlib.h>
#include <string.h>
#include "model.h"

typedef enum CallType {
    CONNECT = 0, // Request Username
    LIST_USERS = 1,
    LIST_GAMES = 2,
    LIST_CHALLENGES = 3,
    CHALLENGE = 4, // Request opponent_user_id
    CONSULT_USER_PROFILE = 5, // Request user_id
    ERROR = 6, // Notify client that an error occurred (for now message to explain why a challenge failed)
    SUCCESS = 7, // Notify client that the previous call was successful (for now used after a challenge is accepted)
    SENT_USER_PROFILE = 8, // Answer to CONSULT_USER_PROFILE -> send the user profile to the server
    RECEIVE_USER_PROFILE = 9

} CallType;


// Serialize User struct into a byte buffer
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
