#include "model.h"

void printUser(User *user) {
    printf("User { username: %s, bio: %s, total_score: %d, total_games %d, total_wins: %d }\n", user->username, user->bio, user->total_score,
           user->total_games, user->total_wins);
}

void printPlayer(Player *player) {
    printf("Player {\n");
    printf("  user: %d\n", player->user_id);
    printf("  score: %d\n", player->score);
    printf("}\n");
}

void printBoard(const int *board, int player) {
    if (player == 1) {
        printf("]\n    - p2: [");
        for (int i = 11; i>=6; i--) {
            printf("%d", board[i]);
            if (i != 6) printf(", ");
        }
        printf("]\n");

        printf("    - p1: [");
        for (int i = 0; i<6; i++) {
            printf("%d", board[i]);
            if (i!=5) printf(", ");
        }
        printf("]\n");

    }if (player == 2) {
        printf("    - p1: [");
        for (int i = 5; i>=0; i--) {
            printf("%d", board[i]);
            if (i!=0) printf(", ");
        }
        printf("]\n");

        printf("    - p2: [");
        for (int i = 6; i<12; i++) {
            printf("%d", board[i]);
            if (i != 11) printf(", ");
        }
        printf("]\n");
    }


}

void printGame(Game *game, int player) {
    printf("Game {\n  board: \n");
    printBoard(game->board, player);
    printf("    SCORE : p1 : %d | p2 : %d\n", game->player1.score, game->player2.score);
    printf("}\n");
}

User newUser(char const *username, char *bio) {
    User user;
    strncpy(user.username, username, sizeof(user.username));
    srand( time( NULL ) );
    user.id = rand() % 100000;
    user.bio = bio;
    user.total_score = 0;
    user.total_games = rand() % 100;
    user.total_wins = 0;

    return user;
}

Player newPlayer(int user_id, int fd) {
    return (Player){
        .score = 0,
        .user_id = user_id,
        .fd = fd
    };
}

Game *newGame(Player *player1, Player *player2) {
    Game *game = (Game *)malloc(sizeof(Game));
    if (!game) return NULL;

    game->player1 = *player1;
    game->player2 = *player2;

    for (int i = 0; i < 12; i++) {
        game->board[i] = 4;
    }
    return game;
}

int moveSeeds(Game *game, int start_position) {
    // move the seeds from the position @start_position to until there is no more seeds to move,
    // then returns the position it stopped on
    int seeds_to_move = game->board[start_position];
    game->board[start_position] = 0;
    int position = start_position;
    while (seeds_to_move > 0) {
        position++;
        if (position == 12) {
            position = 0;
        }
        if (position == start_position) {
            continue;
        }

        game->board[position] += 1;
        seeds_to_move--;
    }
    return position;
}

int collectSeedsAndCountPoints(Game *game, int position, int player) {
    int score = 0;
    int seeds_on_hole;
    if (player==1) {
        if (position<6 && position>=0){
            return score;
        }

        while (1) {
            seeds_on_hole = game->board[position];
            if (seeds_on_hole< 2 || seeds_on_hole > 3) {
                break;
            }
            score += seeds_on_hole;
            game->board[position] = 0;
            position--;
            if (position == 5) {
                break;
            }
        }
        return score;
    }

    if (player==2) {
        if (position<12 && position>=6){
            return score;
        }
        while (1) {
            seeds_on_hole = game->board[position];
            if (seeds_on_hole< 2 || seeds_on_hole > 3) {
                break;
            }
            score += seeds_on_hole;
            game->board[position] = 0;
            position--;
            if (position == - 1) {
                break;
            }
        }
        return score;
    }
}


int playerSeedsLeft(Game *game, int player) {
    int total_seeds = 0;
    if (player == 1) {
       for (int i=0; i<6; i++) {
           total_seeds += game->board[i];
       }
    }
    if (player == 2) {
        for (int i=6; i<12; i++) {
            total_seeds += game->board[i];
        }
    }
    return total_seeds;
}
