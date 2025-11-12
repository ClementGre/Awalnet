#pragma once

#include "../common/model.h"

// Initialize client connection and start network thread
void client_init(const char* server_ip, int port);

// Get notification file descriptor for select()
int client_get_notification_fd(void);

// Send functions for all CallTypes
void send_connect(const char* username);
void send_list_users(void);
void send_challenge(int opponent_id);
void send_consult_user_profile(int user_id);
void send_list_ongoing_games(void);
void send_challenge_answer(int challenger_id, int answer);
void send_user_profile(int request_user_id, uint8_t user_buffer[1024]);
void send_play_made(int move);
void send_lobby_chat(const char* message);
void send_game_chat(const char* message);

// Process incoming network messages (returns 1 if processed, 0 if none available)
int process_network_messages(void);
