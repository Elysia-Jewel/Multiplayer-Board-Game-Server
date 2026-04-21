#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

using namespace std;

void clearScreen() {
    cout << "\033[2J\033[1;1H";
}

bool getInputTimeout(string &input, int seconds) {
    fd_set set;
    struct timeval timeout;

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    // Monitor STDIN for data
    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);

    if (rv > 0) {
        getline(cin >> ws, input); // Read input if available
        return true;
    }
    return false; // Timeout or error
}

int main(int argc, char* argv[]) {
    // Basic CLI validation: Needs the Server IP to connect
    if (argc < 2) {
        cout << "Usage: ./client <server_ip>\n";
        return 0;
    }

    // Create a TCP stream socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(8080); // Server port defined in common.h/server.cpp
    inet_pton(AF_INET, argv[1], &serv.sin_addr);

    // Establish connection to the Board Game Server
    if (connect(sock, (sockaddr*)&serv, sizeof(serv)) < 0) {
        cout << "Connection failed\n";
        return 0;
    }

    char buffer[1024];

    // Main Game Loop: Client reacts to Server instructions
    while (true) {
        memset(buffer, 0, 1024);
        int bytes = read(sock, buffer, 1024);
        if (bytes <= 0) break; // Server closed connection or error

        // Protocol Parsing: Format is "TYPE|DATA"
        string msg(buffer);
        size_t sep = msg.find('|');
        if (sep == string::npos) continue;
        string type = msg.substr(0, sep);
        string data = msg.substr(sep + 1);

        clearScreen();

        /* ---------- STATE MACHINE LOGIC ---------- */
        // State: Disconnect
        if (type == "DISCONNECT") {
            cout << "\n[SERVER MESSAGE]: " << data << endl;
            break; // Exit the loop and close the client
        }

        // State: Active Turn
        if (type == "TURN") {
            cout << "--- TIC TAC TOE (5x5) ---\n  0 1 2 3 4\n" << data << "\n>> YOUR TURN! (row col) [30s limit]: \n";
            
            string move;
            if (getInputTimeout(move, 30)) {
                send(sock, move.c_str(), move.length(), 0);
            } else {
                cout << "\nInput timeout! Connection loss...\n";
                break;
            }
        }

        // State: Passive Waiting
        else if (type == "WAIT") {
            cout << "\nWaiting for other player's move...\n";
        }
        // State: Game Concluded (Display board only)
        else if (type == "FINAL") {
            cout << data << endl;
        }
        // State: Outcome and Prompt for Restart
        else if (type == "WIN" || type == "LOSE" || type == "DRAW") {
            cout << data << endl; 
            if (type == "WIN") cout << "--- YOU WIN! ---" << endl;
            else if (type == "LOSE") cout << "--- YOU LOSE! ---" << endl;
            else cout << "--- DRAW! ---" << endl;

            cout << "Play again? (y/n) [30s limit]: \n";
            string response;
            if (getInputTimeout(response, 30)) {
                send(sock, response.c_str(), response.length(), 0);
            } else {
                cout << "\nNo response detected. Connection loss...\n";
                // send(sock, "n", 1, 0);
                break; 
            }
        }
        // State: Termination and Leaderboard
        else if (type == "SCORES") {
            clearScreen();
            cout << "==========================" << endl;
            cout << data; // Displays final player scores from SHM
            cout << "==========================" << endl;
            cout << "Press Enter to exit...";
            cin.ignore();
            cin.get();
            break; // Exit the loop and close socket
        }

    }

    close(sock);
    return 0;
}