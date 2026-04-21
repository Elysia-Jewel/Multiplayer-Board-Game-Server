# Hybrid Multiplayer Board Game Server

This project is a Concurrent Networked Board Game Server built in C++ for Linux. It implements a **Hybrid Concurrency Architecture** required for the CSN6214 assignment:
* **Clients:** Handled by separate **Processes** (using `fork()`).
* **Server Internals:** Logger and Scheduler handled by **Threads** (using `pthread`).
* **Synchronization:** Uses Process-Shared Mutexes and POSIX Shared Memory.

## Prerequisites

You need a Linux environment with the following installed:
* `g++` (Compiler)
* `make` (Build tool)

To install them on Linux:
```bash
sudo apt update
sudo apt install build-essential
```

## How to Build

Open your terminal in the project folder and run:
```bash
make
```
This will compile two executables: `server` and `client`.

## How to Run

You will need multiple terminal windows to simulate the multiplayer environment.
1. In first terminal:
   ```bash
   ./server
   ```
   The server will start, initialize shared memory, and begin the Logger thread.
3. Open a new terminal window (or tab) for each player and run:
   ```bash
   ./client
   ```
   - **Player 1**: Connects and waits for a move.
   - **Player 2**: Connects in a separate terminal.

## How to Clean Up

To remove the compiled files, logs, and clear the shared memory object from your system:
```bash
make clean
```
*Note: Always run this if the server crashes to ensure the shared memory file (`/dev/shm/boardgame_shm`) is removed.*
