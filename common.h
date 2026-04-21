#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define BOARD_SIZE 5
#define MAX_PLAYERS 3

struct GameState {
    char board[BOARD_SIZE][BOARD_SIZE];

    int current_turn;          // 0,1,2
    int connected_players;
    bool game_over;
    int winner;                // -1 = none, 0..2 = player, 3 = draw

    int board_version;

    int restart_votes[MAX_PLAYERS]; // 0 waiting, 1 yes, 3 left
    bool restart_active;

    int player_scores[MAX_PLAYERS];

    int player_active[MAX_PLAYERS]; // 0: Empty, 1: Active, 2: Disconnected
    time_t last_heartbeat[MAX_PLAYERS];  // Provide timeout functionality


    pthread_mutex_t board_mutex;
};

const char* SHM_NAME = "/boardgame_shm";
const int SHM_SIZE = sizeof(GameState);

#endif
