# Competitive Programming Platform with 1v1 Duels

## Overview
A comprehensive client-server competitive programming platform built in C. The platform supports user authentication, problem-solving, real-time 1v1 duels, an Elo rating system, and a robust backend. The project heavily utilizes core Operating System concepts such as socket programming, multi-threading, concurrency control, inter-process communication (IPC), file locking, and secure sandboxing to execute user-submitted code safely.

## Key OS Concepts Demonstrated
- **Concurrency & Multithreading**: Extensive use of Pthreads, Mutexes, and Semaphores (`job_queue`) to handle multiple concurrent clients and restrict the maximum number of simultaneous execution jobs.
- **Inter-Process Communication (IPC)**: Usage of Anonymous Pipes (`pipe()`) to redirect standard streams (`STDERR`, `STDOUT`) and `fork()` to separate the compilation and execution of arbitrary code from the main server process.
- **System Calls & Sandboxing**: Implements `seccomp-bpf` to filter system calls dynamically, ensuring malicious users cannot access or modify the server. Limits CPU time, memory space, and output file sizes using `setrlimit`.
- **Synchronization**: Resolves race conditions using condition variables (`pthread_cond_t`) for waking threads upon 1v1 duel state changes. Uses `select()` for non-blocking I/O multiplexing when monitoring multiple duel participants simultaneously.
- **File System & Integrity**: Deploys `fcntl()` advisory locks (`F_RDLCK`, `F_WRLCK`, `F_SETLKW`) to manage concurrent reads and writes safely in a custom file-based generic database.

## Architecture and Detailed Codebase Analysis

The repository is modularly divided into three main components: Server, Client, and Shared Utilities.

### 1. Server (`Server/`)
- **`server.c`**: The core backend application. It opens a TCP socket and delegates incoming connections to worker threads (`handleClient()`).
  - **Security & Evaluation**: In `submitProblem()`, it forks child processes to run `gcc` to compile the user's C code. Another process is forked to execute the compiled binary inside a strict sandbox defined by BPF filters.
  - **Duels**: Implements real-time 1v1 duels via `startDuel()`. It employs `select()` on both participants' sockets to avoid busy-waiting, reacting instantly when a user views a problem statement, submits code, or forfeits.
- **`genericDatabase.c / .h`**: An ingenious generic file-based database. It avoids raw reads/writes by establishing `fcntl()` advisory locks on database files to prevent corruption during concurrent accesses. It uses function pointers (`MatchFunction`) for searching records, and supports pagination, updating, inserting, and deleting records of arbitrary sizes.
- **`queryDatabase.c / .h`**: The concrete data access layer built over `genericDatabase.c`. It manages `users.dat` (user auth and roles), `problems.dat` (challenges), and `leaderboard.dat` (Elo ratings).

### 2. Client (`Client/`)
- **`client.c`**: A command-line interface application that connects to the server via TCP sockets. 
  - **Menu Management**: Displays distinct menus based on the server-authenticated Role-Based Access Control (Admin vs Regular User). Admins can add or delete problem data dynamically.
  - **Data Encoding**: Transmits user commands and data structured securely inside JSON packets. 
  - **Remote Execution Interface**: Collects the absolute path to a user's local `.c` solution file, efficiently streams it to the server using the `sendFile()` utility, and gracefully waits for the server's judgment (Accepted, Wrong Answer, Compilation Error, Runtime Error).

### 3. Utility Modules (`Utility/`)
- **`fileTransfer.c / .h`**: Implements chunked data transmission protocols over standard sockets. To prevent memory overflow during massive payload transfers (like large I/O `.in`/`.out` files), `sendFile()` and `receiveFile()` first transmit the total byte size, followed by the content streamed iteratively in manageable 1024-byte chunks. Also handles `readPipe()` for reliable IPC stdout scraping.
- **`elo.c / .h`**: Calculates rating adjustments after duels. Uses the formal Elo mathematical formula taking a `kFactor`, `playerElo`, `opponentElo`, and `score` to return expected gains/losses dynamically.
- **`seccomp-bpf.h`**: A low-level header packed with BPF (Berkeley Packet Filter) macro definitions. It is critical for defining the secure execution wrapper policy used by the server's sandbox (whitelisting calls like `read`, `write`, `exit`, and dropping `execve` or unauthorized filesystem accesses).
- **`cJSON.c / .h` & `jsonUtility.c / .h`**: A lightweight third-party JSON parser mapped perfectly to C structs, utilized heavily by both Server and Client to serialize structured requests and responses.
- **`structs.h` & `codes.h`**: Standardizes shared entities like `User`, `UserRecord`, `LeaderboardEntry`, and `Problem`. `codes.h` defines numeric constants (`LOGIN_ATTEMPT = 1`, `SUBMIT_SOLUTION = 5`) for protocol standardizing.

### Other Essential Files
- **`Makefile`**: A dynamic build system configuration using `wildcard` to automatically discover and link `.c` files in `Server/`, `Client/`, and `Utility/` directories. Output targets generate the `server` and `client` executables cleanly.
- **`report.tex`**: The LaTeX document explicitly detailing the problem statements, architectural challenges, and solutions regarding how Operating Systems concepts were practically applied in the project.

## Building and Executing

To compile the platform:
```bash
make clean
make
```

To run the backend server:
```bash
./server
```

To launch a client terminal:
```bash
./client
```

*Note: The platform connects to `INADDR_ANY` by default when run locally. Use `#define ONLINE_VM` within `client.c` to bind to the Azure remote server configuration if needed.*
