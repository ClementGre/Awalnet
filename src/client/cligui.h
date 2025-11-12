#pragma once


#include "../common/api.h"
#include "../common/model.h"

// Main UI loop - should be called from main after client initialization
void run_client_ui(void);

// Interruptions called by client.c that are called from the server thread and that should take action immediately.
void interrupt_consult_user_profile(int request_user_id);

// Handler functions called by client.c when messages are received
void on_connected(User user);
void on_challenge_received(int challenger_id, char challenger_username[USERNAME_SIZE + 1]);
void on_challenge_request_answer(int challenged_user_id, int answer);
void on_error(int previous_call, char error_msg[256]);
void on_success(void);
void on_list_users(char user_list_buffer[1024]);
void on_list_ongoing_games(char games_list_buffer[1024]);
void on_receive_user_profile(uint8_t buffer[1024]);
void on_challenge_start(char opponent_username[USERNAME_SIZE + 1]);
void on_your_turn(int move_played);
void on_game_over(GAME_OVER_REASON reason);
