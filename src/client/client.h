#pragma once

#include "../common/model.h"

// Initialize client connection and start network thread
void client_init(const char* server_ip, int port);

// Send functions for all CallTypes
void send_connect(const char* username);
void send_list_users(void);
void send_challenge(int opponent_id);
void send_consult_user_profile(int user_id);
void send_list_ongoing_games(void);
void send_challenge_answer(int challenger_id, int answer);
void send_user_profile(int request_user_id, uint8_t user_buffer[1024]);
void send_play_made(int move);

// Process incoming network messages
void process_network_messages(void);
