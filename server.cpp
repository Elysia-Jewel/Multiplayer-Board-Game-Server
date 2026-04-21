#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <csignal>
#include <sys/wait.h> 
#include <cerrno>     
#include <fstream>    
#include "common.h"
#include "logger.h"

using namespace std;

// Pointer to shared memory structure
GameState* game;

/* ---------- HELPERS ---------- */
char getSymbol(int pid) {
    return (pid == 0) ? 'X' : (pid == 1) ? 'O' : 'A';
}

/* ---------- ZOMBIE REAPER (Requirement 3.1) ---------- */
void sigchld_handler(int s) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

/* ---------- THREADS (Requirement 3.2) ---------- */

// Logger thread
void* logger_thread(void* arg) {
    int last_turn = -1;
    LOG_INFO("Logger thread started.");
    
    while (true) {
        sleep(1); 
        
        // Log turn changes and active players
        if (game->current_turn != last_turn) {
            string msg = "Turn changed to Player " + to_string(game->current_turn) + 
                         " | Active Players: " + to_string(game->connected_players);
            LOG_INFO(msg);
            last_turn = game->current_turn;
        }

        if (game->game_over) {
            LOG_INFO("Game over detected. Winner code: " + to_string(game->winner));
            // Wait for a game reset state before resuming turn logging
            while(game->game_over) sleep(1);
        }
    }
    return NULL;
}


// Scheduler thread
void* scheduler_thread(void* arg) {
    while (true) {
        // Only schedule if there is at least one player
        if (!game->game_over && game->connected_players > 0) {
            pthread_mutex_lock(&game->board_mutex);
            int current = game->current_turn;
            time_t now = time(NULL);

            bool should_skip = false;

            // Skip if the player is inactive / disconnected
            if (game->player_active[current] == 0) {
                LOG_INFO("Player " + to_string(current) + " is inactive. Skipping turn.");
                should_skip = true;
            } 
            // Skip if players are active but haven't pulsed a heartbeat in 31 seconds
            else if (now - game->last_heartbeat[current] > 31) {
                LOG_WARNING("Player " + to_string(current) + " heartbeat timeout (31s). Forcing Disconnect.");             
                should_skip = true;
                game->player_active[current] = 0; 
                game->connected_players--;
                
            }

            if (should_skip) {
                LOG_INFO("Scheduling: Skipping Player " + to_string(game->current_turn) + "'s turn.");
                game->current_turn = (game->current_turn + 1) % MAX_PLAYERS;
                game->board_version++;
                LOG_INFO("Scheduling: Skipping Player " + to_string(game->current_turn) + "'s turn.");
            }
            pthread_mutex_unlock(&game->board_mutex);
        }
        usleep(1000000); // Check once per second to reduce log spam
    }
    return NULL;
}

void handle_shutdown(int sig) {
    LOG_INFO("Shutdown signal (SIGINT) received. Saving scores...");
    
    ofstream outFile("scores.txt");
    if (outFile.is_open()) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            outFile << "Player " << i << ": " << game->player_scores[i] << endl;
        }
        outFile.close();
        LOG_INFO("Scores successfully written from SHM to scores.txt.");
    }
    
    // Cleanup SHM
    shm_unlink(SHM_NAME);
    Logger::getInstance().log(LogLevel::INFO, "main", "Server cleaned up. Exiting.");
    exit(0);
}

/* ---------- GAME LOGIC ---------- */
bool checkWin(char board[BOARD_SIZE][BOARD_SIZE], char sym) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j <= BOARD_SIZE - 4; j++) {
            // Horizontal
            if (board[i][j] == sym && board[i][j+1] == sym &&
                board[i][j+2] == sym && board[i][j+3] == sym) return true;
            // Vertical
            if (board[j][i] == sym && board[j+1][i] == sym &&
                board[j+2][i] == sym && board[j+3][i] == sym) return true;
        }
    }
    // Diagonals
    for (int i = 0; i <= BOARD_SIZE - 4; i++) {
        for (int j = 0; j <= BOARD_SIZE - 4; j++) {
            if (board[i][j] == sym && board[i+1][j+1] == sym &&
                board[i+2][j+2] == sym && board[i+3][j+3] == sym) return true;
            if (board[i+3][j] == sym && board[i+2][j+1] == sym &&
                board[i+1][j+2] == sym && board[i][j+3] == sym) return true;
        }
    }
    return false;
}

bool checkDraw(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            if (board[i][j] == ' ') return false;
    return true;
}

/* ---------- PROTOCOL ---------- */
void send_msg(int sock, string type, string data) {
    string pkt = type + "|" + data;
    send(sock, pkt.c_str(), pkt.length(), 0);
}

/* ---------- PLAYER PROCESS (Requirement 3.1) ---------- */
void handle_player(int sock, int pid) {
    char buffer[1024];
    int last_seen = -1;

    LOG_INFO("Child process created for Player " + to_string(pid) + " (PID: " + to_string(getpid()) + ")");

    while (true) {
        
        // Pulse heartbeat every loop iteration
        pthread_mutex_lock(&game->board_mutex);
        if (game->player_active[pid] == 0) {
            pthread_mutex_unlock(&game->board_mutex);
            
            LOG_INFO("Player " + to_string(pid) + " disconnecting due to inactivity.");
            
            send_msg(sock, "DISCONNECT", "You were disconnected for inactivity.");
            close(sock); 
            exit(0); 
        }
        game->last_heartbeat[pid] = time(NULL);
        pthread_mutex_unlock(&game->board_mutex);

        /* GAME OVER */
        if (game->game_over) {

            time_t game_end_time = time(NULL);// 1. Capture the exact time the game ended

            string boardStr = "";
            for (int i = 0; i < BOARD_SIZE; i++) {
                boardStr += to_string(i) + " ";
                for (int j = 0; j < BOARD_SIZE; j++)
                    boardStr += game->board[i][j], boardStr += " ";
                boardStr += "\n";
            }
            string resultType = "DRAW";
            if(game->winner == 3){
                LOG_INFO("It's a draw. Everybody wins!");
            } else if (game->winner == pid){
                resultType = "WIN";
                LOG_INFO("Player " + to_string(pid) + " achieved victory.");
            } else {
                resultType = "LOSE";
            } 
            
            send_msg(sock, resultType, boardStr);

            // Simple wait for exit/restart response
            memset(buffer, 0, 1024);
            int bytes = read(sock, buffer, 1024);

            if (bytes <= 0 || buffer[0] == 'n' || buffer[0] == 'N') {
                LOG_INFO("Player " + to_string(pid) + " chose to exit after game over.");
                string scores = "FINAL LEADERBOARD:\n";
                for(int i=0; i<MAX_PLAYERS; i++) scores += "P" + to_string(i) + ": " + to_string(game->player_scores[i]) + "\n";
                send_msg(sock, "SCORES", scores);
                pthread_mutex_lock(&game->board_mutex);
                game->player_active[pid] = 0; // Important: Clear slot for new players
                game->connected_players--;
                pthread_mutex_unlock(&game->board_mutex);
                close(sock);
                exit(0);
            }else if (buffer[0] == 'y' || buffer[0] == 'Y') {
                LOG_INFO("Player " + to_string(pid) + " voted to restart the game.");
                pthread_mutex_lock(&game->board_mutex);
                game->restart_votes[pid] = 1;

                // Check if ALL active players have voted 'Yes'
                bool all_yes = true;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (game->player_active[i] == 1 && game->restart_votes[i] != 1) {
                        all_yes = false;
                    }
                }
                // If this is the last player to vote 'Yes', reset the game
                if (all_yes && !game->restart_active) {
                    LOG_INFO("All active players voted to restart. Resetting game state.");
                    game->restart_active = true;
                    for (int i = 0; i < BOARD_SIZE; i++)
                        for (int j = 0; j < BOARD_SIZE; j++) game->board[i][j] = ' ';
                    
                    game->winner = -1;
                    game->current_turn = 0;
                    game->board_version++;
                    for (int i = 0; i < MAX_PLAYERS; i++) game->restart_votes[i] = 0;
                    
                    game->game_over = false; // This releases all waiting child processes
                    game->restart_active = false;
                    LOG_INFO("Game reset by Player " + to_string(pid));
                }
                pthread_mutex_unlock(&game->board_mutex);
            }else {
                LOG_INFO("Player " + to_string(pid) + " provided invalid restart response.");
                // Treat invalid characters as "No" to avoid hanging the server
                send_msg(sock, "DISCONNECT", "Invalid choice. Closing connection.");
                pthread_mutex_lock(&game->board_mutex);
                game->player_active[pid] = 0;
                game->connected_players--;
                pthread_mutex_unlock(&game->board_mutex);
                close(sock);
                exit(0);
            }
            while (game->game_over) {
                if (time(NULL) - game_end_time > 60) {
                    LOG_WARNING("Game restart timeout (60s). Disconnecting Player " + to_string(pid));
                    send_msg(sock, "DISCONNECT", "Game closed due to restart timeout.");
                    
                    pthread_mutex_lock(&game->board_mutex);
                    game->player_active[pid] = 0;
                    game->connected_players--;
                    pthread_mutex_unlock(&game->board_mutex);
                    
                    close(sock);
                    exit(0); 
                }
                usleep(100000); // Poll every 100ms
            }
            continue;
        }

        /* MY TURN */
        if (game->current_turn == pid) {
            string boardStr = "";
            for (int i = 0; i < BOARD_SIZE; i++) {
                boardStr += to_string(i) + " ";
                for (int j = 0; j < BOARD_SIZE; j++)
                    boardStr += game->board[i][j], boardStr += " ";
                boardStr += "\n";
            }

            send_msg(sock, "TURN", boardStr);

            memset(buffer, 0, 1024);
            int bytes = read(sock, buffer, 1024);
            if (bytes <= 0) {
                LOG_WARNING("Player " + to_string(pid) + " lost connection.");
                break;
            }

            // Basic parsing of "row col"
            int r = buffer[0] - '0';
            int c = buffer[2] - '0';

            // Synchronize using thread mutex (shared across processes)
            pthread_mutex_lock(&game->board_mutex);
            if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE &&
                game->board[r][c] == ' ') {

                char sym = getSymbol(pid);
                game->board[r][c] = sym;
                game->board_version++;

                LOG_DEBUG("Move logged: Player " + to_string(pid) + " at [" + to_string(r) + "," + to_string(c) + "]");
                string boardState = "\nBoard State (Version " + to_string(game->board_version) + "):\n  0 1 2 3 4\n";

                for (int i = 0; i < BOARD_SIZE; i++) {
                    boardState += to_string(i) + " ";
                    for (int j = 0; j < BOARD_SIZE; j++) {
                        boardState += game->board[i][j];
                        boardState += " ";
                    }
                    boardState += "\n";
                }
                LOG_INFO(boardState);
                // Inside handle_player when a win is detected
                if (checkWin(game->board, sym)) {
                    game->winner = pid;
                    game->player_scores[pid] += 1; 
                    game->game_over = true;
                    
                    // Immediate persistence
                    ofstream outFile("scores.txt");
                    for (int i = 0; i < MAX_PLAYERS; i++) 
                        outFile << "Player " << i << ": " << game->player_scores[i] << endl;
                    
                    LOG_INFO("Game End: Player " + to_string(pid) + " wins.");
                } else if (checkDraw(game->board)) {
                    game->winner = 3;
                    game->game_over = true;
                    LOG_INFO("Game End: Draw.");
                } else {
                    // Cyclic player order
                    game->current_turn = (game->current_turn + 1) % MAX_PLAYERS;
                }
            }
            pthread_mutex_unlock(&game->board_mutex);
        }
        /* WAIT */
        else {
            if (game->board_version > last_seen) {
                string boardStr = "";
                for (int i = 0; i < BOARD_SIZE; i++) {
                    boardStr += to_string(i) + " ";
                    for (int j = 0; j < BOARD_SIZE; j++)
                        boardStr += game->board[i][j], boardStr += " ";
                    boardStr += "\n";
                }
                send_msg(sock, "WAIT", boardStr);
                last_seen = game->board_version;
            }
            usleep(10000);
        }
    }
    
    pthread_mutex_lock(&game->board_mutex);
    game->player_active[pid] = 0; // Free the slot
    game->connected_players--;
    pthread_mutex_unlock(&game->board_mutex);

    close(sock);
    LOG_INFO("Player " + to_string(pid) + " process exiting.");
    exit(0);
}

/* ---------- MAIN ---------- */
int main() {
    // 1. Initialize Logger 
    Logger::getInstance().init("game.log");
    LOG_INFO("Server environment initialization started.");
    
    // 2. Setup Shared Memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, SHM_SIZE);
    game = (GameState*)mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // Load scores into SHM
    ifstream inFile("scores.txt");
    if (inFile.is_open()) {
        string line;
        int p_idx = 0;
        while (getline(inFile, line) && p_idx < MAX_PLAYERS) {
            // Simple parser: "Player X: Score"
            size_t pos = line.find(": ");
            if (pos != string::npos) {
                game->player_scores[p_idx] = stoi(line.substr(pos + 2));
            }
            p_idx++;
        }
        inFile.close();
        LOG_INFO("Loaded scores from scores.txt into SHM.");
    } else {
        LOG_WARNING("scores.txt not found. Creating new score entries.");
        for(int i=0; i<MAX_PLAYERS; i++) game->player_scores[i] = 0;
    }

    // Register Shutdown Handler
    signal(SIGINT, handle_shutdown);

    // 3. Initialize Shared Mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&game->board_mutex, &attr);

    // 4. Reset Game State in SHM
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            game->board[i][j] = ' ';
    game->current_turn = 0;
    game->connected_players = 0;
    game->game_over = false;
    game->winner = -1;
    game->board_version = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        game->restart_votes[i] = 0;

    // 5. Setup Zombie Reaping
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        LOG_ERROR("Failed to setup sigaction for SIGCHLD.");
        exit(1);
    }

    // 6. Spawn Server Internal Threads
    pthread_t log_t, sched_t;
    if (pthread_create(&log_t, NULL, logger_thread, NULL) != 0 ||
        pthread_create(&sched_t, NULL, scheduler_thread, NULL) != 0) {
        LOG_ERROR("Critical failure spawning server threads.");
        exit(1);
    }

    // 7. Network Setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Bind failed on port 8080.");
        exit(1);
    }
    listen(server_fd, MAX_PLAYERS);

    LOG_INFO("Server listening on port 8080. Ready for players.");

    // 8. Connection Accept Loop
    while (true) {
        
        int sock = accept(server_fd, NULL, NULL);
        if (sock < 0) {
            if (errno == EINTR) continue; // Signal interrupted
            LOG_ERROR("Socket accept error.");
            continue;
        }

        if (game->connected_players < MAX_PLAYERS) {
            int pid = -1;
            // Find first free slot
            for(int i=0; i<MAX_PLAYERS; i++) {
                if(game->player_active[i] == 0) { pid = i; break; } 
            }

            pthread_mutex_lock(&game->board_mutex);
            game->player_active[pid] = 1; 
            game->last_heartbeat[pid] = time(NULL); 
            game->connected_players++; 
            pthread_mutex_unlock(&game->board_mutex);

            if (fork() == 0) { 
                close(server_fd);
                handle_player(sock, pid); 
            }
            close(sock);
        } else {
            close(sock);
        }
    }

    return 0;
}