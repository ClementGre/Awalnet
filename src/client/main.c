#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cligui.h"
#include "client.h"
#include "../common/model.h"

#define PORT 8080

int main(void) {
    /* Sequential actions:
     * - Request connection (providing username). Connection refused if someone is already connected with this username.
     * - Possible actions:
     *   - List users and their bios and the current games
     *   - Check current game state (this might allow to resume a game after a network issue)
     *   - Set a bio
     *   - Add/Remove friend
     *   - Enable/Disable private mode
     *   - Observe a game state
     *   - Review an old game
     *   - Challenge someone, List challenges, Deny a challenge, Accept a challenge and join a game
     */



    // Initialize client connection
    client_init("127.0.0.1", PORT);

    // Run the main UI loop (blocks until exit)
    run_client_ui();

    return 0;
}
