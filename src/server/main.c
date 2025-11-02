#include <stdlib.h>

#include "../common/model.h"
#include "server.h"


int main(void) {

    start_server();

    // Store of all User
    User **users = malloc(128 * sizeof *users);

    char username1[USERNAME_SIZE+1];
    char username2[USERNAME_SIZE+1];

    printf("Enter the username of player 1 : ");
    scanf("%s", username1);
    printf("Enter the username of player 2 : ");
    scanf("%s", username2);

    User user1 = newUser(username1, "");
    User user2 = newUser(username2, "");
    users[0] = &user1;
    users[1] = &user2;

    int fd = 0; // unused for now
    Player player1 = newPlayer(user1.id, fd);
    Player player2 = newPlayer(user2.id, fd);

    // initialize the game
    Game *game = newGame(&player1, &player2);
    
    // display both users
    printUser(&user1);
    printUser(&user2);

    // display both players
    printPlayer(&player1);
    printPlayer(&player2);


    /*   GAME STARTS
     * each turn, one player gets to select one of his holes and move the seeds (one by one) it contains to te following holes (trigonometric direction)
     * then, if he put seeds in his opponents holes and those holes contains 2 or 3 seeds, takes then. Stops when there is more of less than 2/3 seeds.
     *
     * WIN CONDITION
     * A player wins when he has a score of 25 or looses when he has 6 or less seeds remaining
     */
    int rounds = 0;
    while (1) {

        if (rounds == MAX_ROUNDS) {
            printf("\n---------------- MAX ROUND PLAYED : DRAW --------------\n");

            break;
        }

        if (player1.score == WINNING_SCORE || playerSeedsLeft(game, 2) < 6) {
            printf("\n---------------- PLAYER 1 WON !!! --------------\n");
             break;
        }

        if (player2.score == WINNING_SCORE || playerSeedsLeft(game, 1) < 6) {
            printf("\n---------------- PLAYER 2 WON !!! --------------\n");
            break;
        }
        // -------------------- PLAYER 1 ------------------------------------
        printGame(game, 1);
        printf("SCORES :\n Player 1 %d VS Playe 2 %d\n", player1.score, player2.score);
        // player 1 plays
        // displays the board
        int position = 0;
        printf("PLAYER 1 - ENTER A CELL POSITION TO MOVE SEEDS FROM : ");
        scanf("%d", &position);

        // if exceds 5 of <0
        while (position < 1 || position > 6) {
            printf("PLAYER 1 - ENTER A CELL POSITION BETWEEN 1 AND 6: ");
            scanf("%d", &position);
        }

        // if the player has 0 seeds in that hole, he needs to select another one
        while (game->board[position - 1]== 0) {
            printf("PLAYER 1 - ENTER A CELL POSITION WHERE YOU HAVE MORE THAN 1 SEED : ");
            scanf("%d", &position);
        }



        // then moves the seeds to other cells
        printf("moving seeds to other cells\n");
        // - 5 the ensure it matches the stored indexes
        int position_of_last_put_seed = moveSeeds(game, position  - 1);
        printf("stopped on position :%d\n", position_of_last_put_seed);

        player1.score += collectSeedsAndCountPoints(game, position_of_last_put_seed, 1);
        printf("score of player 1 : %d\n", player1.score);


        // -------------------- PLAYER 2 ------------------------------------
        printGame(game, 2);
        // player 1 plays
        // displays the board
        position = 0;
        printf("PLAYER 2 - ENTER A CELL POSITION TO MOVE SEEDS FROM : ");
        scanf("%d", &position);

        // if exceds 5 of <0
        while (position < 1 || position > 6) {
            printf("PLAYER 1 - ENTER A CELL POSITION BETWEEN 1 AND 6: ");
            scanf("%d", &position);
        }

        // if the player has 0 seeds in that hole, he needs to select another one
        while (game->board[5 + position ]== 0) {
            printf("PLAYER 2 - ENTER A CELL POSITION WHERE YOU HAVE MORE THAN 1 SEED : ");
            scanf("%d", &position);
        }



        // then moves the seeds to other cells
        printf("moving seeds to other cells\n");
        position_of_last_put_seed = moveSeeds(game, position + 5);
        printf("stopped on position :%d\n", position_of_last_put_seed);

        player2.score += collectSeedsAndCountPoints(game, position_of_last_put_seed, 2);
        printf("score of player 2 : %d\n", player2.score);

        rounds++;




    }

    return 0;
}


