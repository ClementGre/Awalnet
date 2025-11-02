#pragma once
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdlib.h>


#define MAX_ROUNDS 10
#define MIN_SEEDS 6
#define WINNING_SCORE 25

enum {
    USERNAME_SIZE = 32,
    BIO_SIZE = 10 * 128
};

typedef struct User {
    char username[USERNAME_SIZE + 1];
    int id;
    char *bio;
    int total_score;
    int total_games;
    int total_wins;
} User;

typedef struct Player {
    int score;
    int user_id; // No need to store the whole User struct here, only the id matters
    int fd;
} Player;

typedef struct GamePreview {
    int player1_id;
    int player2_id;
} GamePreview;

typedef struct Game {
    int id;
    Player player1; // The player that plays first
    Player player2;

    /*
     * Array content: [p1-0, p1-1, p1-2, p1-3, p1-4, p1-5, p2-0, p2-1, p2-2, p2-3, p2-4, p2-5]
     * For p1, displayed as:
     *   p2-5 p2-4 p2-3 p2-2 p2-1 p2-0
     *   p1-0 p1-1 p1-2 p1-3 p1-4 p1-5
     * For p2, displayed as:
     *   p1-5 p1-4 p1-3 p1-2 p1-1 p1-0
     *   p2-0 p2-1 p2-2 p2-3 p2-4 p2-5
     */
    int board[12];

    /*
     * Each char stores:
     *  - player1 played index (1-6) in the 0b00001111 bits
     *  - player2 played index (1-6) in the 0b11110000 bits
     *  If the index is 0, it means it hasn’t been played
     */
    char played[128];

} Game;

void printUser(User *user);
void printPlayer(Player *player);
void printBoard(const int *board, int  player);
void printGame(Game *game, int player);

User newUser(const char* username, char* bio);
Player newPlayer(int user_id, int fd);
Game *newGame(Player *player1, Player *player2);
int moveSeeds(Game *game, int start_position);
int collectSeedsAndCountPoints(Game *game, int position, int player);
int playerSeedsLeft(Game *game, int player);

