# Compiler to use (g++ for C++)
CXX = g++

# Compiler Flags:
# -Wall    : Enable all standard warnings (helps debug)
# -g       : Add debugging information (for gdb)
# -pthread : REQUIRED. Enables threading support for the Hybrid model
CXXFLAGS = -Wall -g -pthread

# Linker Flags:
# -lrt     : REQUIRED. Links the "Real-Time" library for shared memory (shm_open)
LDFLAGS = -lrt

# Default target: builds both server and client
all: server client

# Rule to build the Server
server: server.cpp common.h
	$(CXX) $(CXXFLAGS) -o server server.cpp $(LDFLAGS)

# Rule to build the Client
client: client.cpp common.h
	$(CXX) $(CXXFLAGS) -o client client.cpp $(LDFLAGS)

# Clean up build files and logs
clean:
	rm -f server client game.log scores.txt /dev/shm/boardgame_shm

# Helper to run the server quickly
run: server
	./server