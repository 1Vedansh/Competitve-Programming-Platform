#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/select.h>

#include "../Utility/structs.h"
#include "../Utility/codes.h"
#include "../Utility/cJSON.h"
#include "../Utility/jsonUtility.h"
#include "../Utility/fileTransfer.h"
#include "../Utility/seccomp-bpf.h"
#include "../Utility/elo.h"

#include "genericDatabase.h"
#include "queryDatabase.h"

#define MAX_USERS 5
#define MAX_CHALLENGES 5
#define MAX_JOBS 2
int activeUsers = 0;
int activeChallenges = 0;

User* activeUserList[MAX_USERS];
Challenge* activeChallengesList[MAX_CHALLENGES];

pthread_mutex_t activeUsersLock;
pthread_mutex_t challengeLock;

#define DEFAULT_ELO 1000

int seedRandomNumber(int seed, int mod){
    srand(seed);
    return rand() % mod;
}

sem_t job_queue;

int submitProblem(int problemID, char* buffer, char* tempSolutionPath);

User* getActiveUserByUsername(char *username){
    pthread_mutex_lock(&activeUsersLock);
    for(int i = 0; i < activeUsers; i++){
        if(activeUserList[i] != NULL && strcmp(activeUserList[i]->username, username) == 0){
            User *user = activeUserList[i];
            pthread_mutex_unlock(&activeUsersLock);
            return user;
        }
    }
    pthread_mutex_unlock(&activeUsersLock);
    return NULL;
}

void setDuelState(User *user, int state){
    if(user == NULL){
        return;
    }

    pthread_mutex_lock(&user->duelLock);
    user->duelRequest = state;
    pthread_cond_broadcast(&user->duelCond);
    pthread_mutex_unlock(&user->duelLock);
}

void sendJsonStatus(int socket, int status, char *message){
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "status", status);
    if(message != NULL){
        cJSON_AddStringToObject(response, "message", message);
    }

    char *json_str = cJSON_PrintUnformatted(response);
    write(socket, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(response);
}

void serverInit(){
    if(pthread_mutex_init(&activeUsersLock, NULL) < 0){
        perror("failed to initialize mutex");
        exit(-1);
    }

    if(pthread_mutex_init(&challengeLock, NULL) < 0){
        perror("failed to initialize mutex");
        exit(-1);
    }

    if(sem_init(&job_queue, 0, MAX_JOBS) < 0){
        perror("failed to initialize semaphore");
        exit(-1);
    }

    int fd = open("./database/users.dat", O_RDONLY);

    if(fd < 0){
        fd = open("./database/users.dat", O_CREAT | O_WRONLY, 0644);
        if(fd < 0){
            perror("failed to create users.dat");
            exit(-1);
        }

        int zero = 1;
        write(fd, &zero, sizeof(int));

        UserRecord adminUser = {};
        strcpy(adminUser.username, "admin");
        strcpy(adminUser.password, "admin");
        adminUser.privilege = ADMIN_USER;

        write(fd, &adminUser, sizeof(UserRecord));

        close(fd);
    }else{
        close(fd);
    }

    fd = open("./database/problems.dat", O_RDONLY);

    if(fd < 0){
        fd = open("./database/problems.dat", O_CREAT | O_WRONLY, 0644);
        if(fd < 0){
            perror("failed to create problems.dat");
            exit(-1);
        }

        int zero = 0;
        write(fd, &zero, sizeof(int));

        close(fd);
    }else{
        close(fd);
    }

    fd = open("./database/leaderboard.dat", O_RDONLY);

    if(fd < 0){
        fd = open("./database/leaderboard.dat", O_CREAT | O_WRONLY, 0644);
        if(fd < 0){
            perror("failed to create leaderboard.dat");
            exit(-1);
        }

        int zero = 0;
        write(fd, &zero, sizeof(int));

        close(fd);
    }else{
        close(fd);
    }
}

int updateDuelElos(char *winner, char *loser){
    LeaderboardEntry *winnerEntry = getLeaderboardEntry(winner);
    LeaderboardEntry *loserEntry = getLeaderboardEntry(loser);

    if(winnerEntry == NULL || loserEntry == NULL){
        if(winnerEntry != NULL){
            free(winnerEntry);
        }
        if(loserEntry != NULL){
            free(loserEntry);
        }
        return FAILURE;
    }

    int winnerElo = winnerEntry->elo;
    int loserElo = loserEntry->elo;

    free(winnerEntry);
    free(loserEntry);

    int eloChange = calculateEloChange(winnerElo, loserElo, 1, 50);
    
    LeaderboardEntry updatedWinner;
    strcpy(updatedWinner.username, winner);
    updatedWinner.elo = winnerElo + eloChange;
    updateLeaderboardEntry(&updatedWinner);

    LeaderboardEntry updatedLoser;
    strcpy(updatedLoser.username, loser);
    updatedLoser.elo = loserElo - eloChange;
    updateLeaderboardEntry(&updatedLoser);
    return SUCCESS;
}

int handleDuelSocketMessage(Problem *problem, int socket, int opponentSocket, char *playerName, char *opponentName, char *buffer, int bufferSize){
    int bytesRead = read(socket, buffer, bufferSize - 1);
    if(bytesRead <= 0){
        // Do not send unsolicited message; caller will handle it
        return 0;
    }

    buffer[bytesRead] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    if(json == NULL){
        sendJsonStatus(socket, FAILURE, "Invalid duel request");
        return 1;
    }

    int operationCode = json_get_int(json, "op");
    cJSON_Delete(json);

    if(operationCode == VIEW_DUEL_PROBLEM_STATEMENT){
        char problemStatementPath[200];
        sprintf(problemStatementPath, "./problems/%d_statement.txt", problem->id);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "status", SUCCESS);
        cJSON_AddStringToObject(response, "title", problem->title);
        cJSON_AddNumberToObject(response, "difficulty", problem->difficulty);

        char *json_str = cJSON_PrintUnformatted(response);
        write(socket, json_str, strlen(json_str));
        free(json_str);
        cJSON_Delete(response);

        sendFile(socket, problemStatementPath, getFileSize(problemStatementPath));
        return 1;
    }

    if(operationCode == SUBMIT_DUEL_SOLUTION){
        char tempSolutionPath[200];
        char resultBuffer[1000];
        sprintf(tempSolutionPath, "./tempSolutions/duel_%d_%s_%ld.c", problem->id, playerName, (long)pthread_self());

        sendJsonStatus(socket, SUCCESS, "Ready to receive solution");
        if(receiveFile(socket, tempSolutionPath) < 0){
            sendJsonStatus(socket, FAILURE, "Failed to receive solution");
            deleteFile(tempSolutionPath);
            return 1;
        }

        if(submitProblem(problem->id, resultBuffer, tempSolutionPath) < 0){
            sendJsonStatus(socket, FAILURE, "Error judging solution");
            return 1;
        }

        if(strcmp(resultBuffer, "Accepted") == 0){
            updateDuelElos(playerName, opponentName);
            sendJsonStatus(socket, GO_TO_USER_MENU, "Accepted. You won the duel.");
            return 0;
        }

        sendJsonStatus(socket, SUCCESS, resultBuffer);
        return 1;
    }

    if(operationCode == FORFEIT_DUEL){
        updateDuelElos(opponentName, playerName);
        sendJsonStatus(socket, GO_TO_USER_MENU, "You forfeited the duel.");
        return 0;
    }

    sendJsonStatus(socket, FAILURE, "Unknown duel operation");
    return 1;
}

void* startDuel(void* arg){
    Duel* arena = (Duel*)arg;
    char buffer[1024];

    Problem* problem = getRandomVisibleProblem();
    User *player1 = getActiveUserByUsername(arena->player1);
    User *player2 = getActiveUserByUsername(arena->player2);

    setDuelState(player1, 1);
    setDuelState(player2, 1);

    if(problem == NULL){
        sendJsonStatus(arena->socket1, GO_TO_USER_MENU, "No visible problems are available for the duel.");
        sendJsonStatus(arena->socket2, GO_TO_USER_MENU, "No visible problems are available for the duel.");
        setDuelState(player1, 0);
        setDuelState(player2, 0);
        free(arena);
        pthread_exit(NULL);
    }

    int p1InDuel = 1;
    int p2InDuel = 1;
    int duelWinner = 0; // 0 = none, 1 = p1 win, 2 = p2 win
    
    while(p1InDuel || p2InDuel){
        fd_set readfds;
        FD_ZERO(&readfds);
        
        int maxFd = -1;
        if(p1InDuel){
            FD_SET(arena->socket1, &readfds);
            if(arena->socket1 > maxFd) maxFd = arena->socket1;
        }
        if(p2InDuel){
            FD_SET(arena->socket2, &readfds);
            if(arena->socket2 > maxFd) maxFd = arena->socket2;
        }

        int activity = select(maxFd + 1, &readfds, NULL, NULL, NULL);

        if(activity < 0){
            if(errno == EINTR){
                continue;
            }
            perror("duel select failed");
            break;
        }

        if(p1InDuel && FD_ISSET(arena->socket1, &readfds)){
            if(duelWinner != 0){
                int bytesRead = read(arena->socket1, buffer, sizeof(buffer) - 1);
                if(bytesRead > 0) {
                    sendJsonStatus(arena->socket1, GO_TO_USER_MENU, "The duel has ended. Your opponent already solved the problem or forfeited. You lost the duel.");
                }
                p1InDuel = 0;
                setDuelState(player1, 0);
            } else {
                int res = handleDuelSocketMessage(problem, arena->socket1, arena->socket2, arena->player1, arena->player2, buffer, sizeof(buffer));
                if(res == 0) {
                    p1InDuel = 0;
                    duelWinner = 1;
                    setDuelState(player1, 0);
                }
            }
        }

        if(p2InDuel && FD_ISSET(arena->socket2, &readfds)){
            if(duelWinner != 0){
                 int bytesRead = read(arena->socket2, buffer, sizeof(buffer) - 1);
                 if(bytesRead > 0){
                     sendJsonStatus(arena->socket2, GO_TO_USER_MENU, "The duel has ended. Your opponent already solved the problem or forfeited. You lost the duel.");
                 }
                 p2InDuel = 0;
                 setDuelState(player2, 0);
            } else {
                int res = handleDuelSocketMessage(problem, arena->socket2, arena->socket1, arena->player2, arena->player1, buffer, sizeof(buffer));
                if(res == 0){
                    p2InDuel = 0;
                    duelWinner = 2;
                    setDuelState(player2, 0);
                }
            }
        }
    }

    free(problem);
    free(arena);
    pthread_exit(NULL);
}

int processDuelRequest(char* challenger, char* target){
    pthread_mutex_lock(&challengeLock);
    
    for(int i = 0; i < activeChallenges; i++){
        if((activeChallengesList[i] != NULL) && (strcmp(activeChallengesList[i]->challenger, target) == 0) && (strcmp(activeChallengesList[i]->target, challenger) == 0)){
            free(activeChallengesList[i]);
            activeChallengesList[i] = NULL;

            activeChallengesList[i] = activeChallengesList[activeChallenges - 1];
            activeChallengesList[activeChallenges - 1] = NULL;
            activeChallenges--;

            pthread_mutex_unlock(&challengeLock);
            return MATCH_FOUND;
        }
    }

    if(activeChallenges >= MAX_CHALLENGES){
        pthread_mutex_unlock(&challengeLock);
        return FAILURE;
    }

    Challenge* newChallenge = (Challenge*)malloc(sizeof(Challenge));
    strcpy(newChallenge->challenger, challenger);
    strcpy(newChallenge->target, target);
    activeChallengesList[activeChallenges] = newChallenge;
    activeChallenges++;

    pthread_mutex_unlock(&challengeLock);
    return ADDED_TO_CHALLENGE_QUEUE;
}

int removeDuelRequest(char* challenger){
    pthread_mutex_lock(&challengeLock);
    
    for(int i = 0; i < activeChallenges; i++){
        if((activeChallengesList[i] != NULL) && (strcmp(activeChallengesList[i]->challenger, challenger) == 0)){
            free(activeChallengesList[i]);
            activeChallengesList[i] = NULL;

            activeChallengesList[i] = activeChallengesList[activeChallenges - 1];
            activeChallengesList[activeChallenges - 1] = NULL;
            activeChallenges--;

            pthread_mutex_unlock(&challengeLock);
            return SUCCESS;
        }
    }

    pthread_mutex_unlock(&challengeLock);
    return FAILURE;
}


int getUserSocket(char* username){
    pthread_mutex_lock(&activeUsersLock);
    for(int i = 0; i < activeUsers; i++){
        if(activeUserList[i] != NULL && strcmp(activeUserList[i]->username, username) == 0){
            int socket = activeUserList[i]->socket;
            pthread_mutex_unlock(&activeUsersLock);
            return socket;
        }
    }
    pthread_mutex_unlock(&activeUsersLock);
    return -1;
}

int findActiveUserIndex(char* username){
    pthread_mutex_lock(&activeUsersLock);
    for(int i = 0; i < activeUsers; i++){
        if(activeUserList[i] != NULL && strcmp(activeUserList[i]->username, username) == 0){
            pthread_mutex_unlock(&activeUsersLock);
            return i;
        }
    }
    pthread_mutex_unlock(&activeUsersLock);
    return -1;
}

int removeActiveUser(char* username){
    pthread_mutex_lock(&activeUsersLock);
    for(int i = 0; i < activeUsers; i++){
        if(activeUserList[i] != NULL && strcmp(activeUserList[i]->username, username) == 0){
            // free(activeUserList[i]);
            activeUserList[i] = activeUserList[activeUsers - 1];
            activeUserList[activeUsers - 1] = NULL;
            activeUsers--;
            pthread_mutex_unlock(&activeUsersLock);
            return 0;
        }
    }
    pthread_mutex_unlock(&activeUsersLock);

    return -1;
}

int addActiveUser(User* user){
    pthread_mutex_lock(&activeUsersLock);
    if(activeUsers >= MAX_USERS){
        pthread_mutex_unlock(&activeUsersLock);
        return -1;
    }

    activeUserList[activeUsers] = user;
    activeUsers++;
    pthread_mutex_unlock(&activeUsersLock);
    return 0;
}

int submitProblem(int problemID, char* buffer, char* tempSolutionPath){
    sem_wait(&job_queue);

    int pipefd[2], pipefd1[2];
    
    if(pipe(pipefd) < 0){
        perror("failed to create pipe");
        deleteMultipleFiles(1, tempSolutionPath);
        sem_post(&job_queue);
        return FAILURE;
    }
    
    char tempExecutablePath[200];
    time_t currentTime = time(NULL);
    int randomNum = seedRandomNumber(time(NULL), 100000);
    sprintf(tempExecutablePath, "./tempSolutions/%d_%d_%d_%ld_executable", problemID, randomNum, (int)currentTime, (long) pthread_self());

    int pid = fork();

    if(pid == 0){
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        execlp("gcc", "gcc", "-std=c11", "-static", "-DONLINE_JUDGE", "-lm", "-O2", "-o", tempExecutablePath, tempSolutionPath, (char *)NULL);
        exit(-1);
    }

    close(pipefd[1]);
    char buffer2[1000];
    int status;

    int bytesRead = readPipe(pipefd[0], buffer2, sizeof(buffer2), "Compilation error: ");
    close(pipefd[0]);

    waitpid(pid, &status, 0);

    if(!WIFEXITED(status) || WEXITSTATUS(status) != 0){
        if(bytesRead > 0){
            strcpy(buffer, buffer2);
        }else{
            strcpy(buffer, "Compilation error: Unknown problem");
        }
        deleteMultipleFiles(2, tempExecutablePath, tempSolutionPath);
        sem_post(&job_queue);
        return FAILURE;
    }

    char problemInputPath[200];
    char problemOutputPath[200];
    char tempOutputPath[250];

    sprintf(problemInputPath, "./problems/%d_input.in", problemID);
    sprintf(problemOutputPath, "./problems/%d_output.out", problemID);
    sprintf(tempOutputPath, "%s_output.out", tempExecutablePath);

    Problem* problem;
    if((problem = getProblem(problemID)) == NULL){
        strcpy(buffer, "Problem not found");
        deleteMultipleFiles(3, tempExecutablePath, tempOutputPath, tempSolutionPath);
        sem_post(&job_queue);
        return FAILURE;
    }
    free(problem);

    if(pipe(pipefd1) < 0){
        perror("failed to create pipe");
        deleteMultipleFiles(3, tempExecutablePath, tempOutputPath, tempSolutionPath);
        sem_post(&job_queue);
        return FAILURE;
    }

    pid = fork();

    if(pid == 0){
        close(pipefd1[0]);

        int inFD = open(problemInputPath, O_RDONLY);
        int outFD = open(tempOutputPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (inFD < 0 || outFD < 0) {
            perror("Server Error: Missing test case files");
            exit(1);
        }

        dup2(inFD, STDIN_FILENO);
        dup2(outFD, STDOUT_FILENO);
        dup2(pipefd1[1], STDERR_FILENO);

        close(inFD);
        close(outFD);
        close(pipefd1[1]);

        struct rlimit rl;
        rl.rlim_cur = 2;
        rl.rlim_max = 2;
        setrlimit(RLIMIT_CPU, &rl);

        struct rlimit rl_mem;
        rl_mem.rlim_cur = 256 * 1024 * 1024;
        rl_mem.rlim_max = 256 * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl_mem);

        struct rlimit rl_nproc;
        rl_nproc.rlim_cur = 0;
        rl_nproc.rlim_max = 0;
        setrlimit(RLIMIT_NPROC, &rl_nproc);

        struct rlimit rl_fsize;
        rl_fsize.rlim_cur = 10 * 1024 * 1024;
        rl_fsize.rlim_max = 10 * 1024 * 1024;
        setrlimit(RLIMIT_FSIZE, &rl_fsize);

        struct sock_filter filter[] = {
            VALIDATE_ARCHITECTURE,
            EXAMINE_SYSCALL,
            
            ALLOW_SYSCALL(read),
            ALLOW_SYSCALL(write),
            ALLOW_SYSCALL(readv),
            ALLOW_SYSCALL(writev),
            ALLOW_SYSCALL(close),
            ALLOW_SYSCALL(fstat),
            ALLOW_SYSCALL(newfstatat),
            ALLOW_SYSCALL(lseek),
            
            ALLOW_SYSCALL(mmap),
            ALLOW_SYSCALL(mprotect),
            ALLOW_SYSCALL(munmap),
            ALLOW_SYSCALL(brk),
            ALLOW_SYSCALL(mremap),
            
            ALLOW_SYSCALL(execve),
            ALLOW_SYSCALL(exit),
            ALLOW_SYSCALL(exit_group),
            
            ALLOW_SYSCALL(arch_prctl),
            ALLOW_SYSCALL(set_tid_address),
            ALLOW_SYSCALL(set_robust_list),
            ALLOW_SYSCALL(rseq),
            ALLOW_SYSCALL(uname),
            ALLOW_SYSCALL(sysinfo),
            ALLOW_SYSCALL(readlink),
            ALLOW_SYSCALL(getrandom),
            ALLOW_SYSCALL(prlimit64),
            ALLOW_SYSCALL(access),
            ALLOW_SYSCALL(faccessat),
            
            KILL_PROCESS,
        };

        struct sock_fprog prog = {
            .len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
            .filter = filter,
        };

        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);

        execlp(tempExecutablePath, tempExecutablePath, (char *) NULL);
        exit(-1);
    }

    close(pipefd1[1]);

    bytesRead = readPipe(pipefd1[0], buffer2, sizeof(buffer2), "Runtime error: ");
    close(pipefd1[0]);

    waitpid(pid, &status, 0);

    if ((!WIFEXITED(status)) || (WEXITSTATUS(status) != 0)) {
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            switch(sig) {
                case SIGSEGV: strcpy(buffer, "Runtime Error: Segmentation Fault (Invalid memory access)"); break;
                case SIGFPE:  strcpy(buffer, "Runtime Error: Floating Point Exception (Division by zero)"); break;
                case SIGSYS:  strcpy(buffer, "Runtime Error: Bad System Call (Sandbox Violation)"); break;
                case SIGXCPU: strcpy(buffer, "Time Limit Exceeded (CPU Limit)"); break;
                case SIGXFSZ: strcpy(buffer, "Output Limit Exceeded (Printed too much data)"); break;
                case SIGABRT: strcpy(buffer, "Runtime Error: Aborted (Assertion failed)"); break;
                case SIGILL:  strcpy(buffer, "Runtime Error: Illegal Instruction"); break;
                case SIGBUS:  strcpy(buffer, "Runtime Error: Bus Error (Memory alignment issue)"); break;
                case SIGKILL: strcpy(buffer, "Runtime Error: Process Killed (Out of Memory or Timeout)"); break;
                default:      sprintf(buffer, "Runtime Error: Killed by OS fatal signal %d", sig); break;
            }
        } 
        else {
            if (bytesRead > 15) {
                strcpy(buffer, buffer2);
            } else {
                sprintf(buffer, "Runtime Error: Exited with non-zero code %d", WEXITSTATUS(status));
            }
        }
        deleteMultipleFiles(3, tempExecutablePath, tempOutputPath, tempSolutionPath);
        sem_post(&job_queue);

        return FAILURE;
    }

    int judgeResult = compareFiles(tempOutputPath, problemOutputPath);
    if(judgeResult == SUCCESS){
        strcpy(buffer, "Accepted");
    }else if(judgeResult == FAILURE){
        strcpy(buffer, "Wrong Answer");
    }else{
        strcpy(buffer, "Error in judging");
    }

    deleteMultipleFiles(3, tempExecutablePath, tempOutputPath, tempSolutionPath);

    sem_post(&job_queue);
    return SUCCESS;
}

void* handleClient(void* arg){
    User* user = (User*)arg;

    pthread_mutex_init(&user->duelLock, NULL);
    pthread_cond_init(&user->duelCond, NULL);

    char username[100];
    char password[100];
    int socket = user->socket;

    char buffer[1000];
    char resultBuffer[1000];
    
    while(1){
        pthread_mutex_lock(&user->duelLock);

        while(user->duelRequest == 1){
            pthread_cond_wait(&user->duelCond, &user->duelLock);
        }

        pthread_mutex_unlock(&user->duelLock);

        int bytesRead = read(socket, buffer, sizeof(buffer));
        if(bytesRead <= 0){
            removeActiveUser(user->username);
            free(user);
            close(socket);
            printf("Client goes bye bye\n");
            return NULL;
        }

        int operationCode;
        cJSON *json = cJSON_Parse(buffer);
        operationCode = json_get_int(json, "op");

        if(operationCode == LOGIN_ATTEMPT){
            strcpy(username, json_get_string(json, "username"));
            strcpy(password, json_get_string(json, "password"));
            
            int status;
            UserRecord* userRecord = getUserRecord(username);

            char *message;

            if(user->loggedIn || findActiveUserIndex(username) != -1){
                status = FAILURE;
                message = "User already logged in";
            }else if(userRecord == NULL || strcmp(userRecord->password, password) != 0){
                message = "Invalid username or password";
                status = FAILURE;
            }else{
                user->loggedIn = 1;
                strcpy(user->username, username);
                user->privilege = userRecord->privilege;

                if(addActiveUser(user) < 0){
                    status = FAILURE;
                    message = "Server full. Try again later.";
                    memset(user, 0, sizeof(User));
                }else{
                    status = SUCCESS;
                    message = "Login successful";
                }
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            cJSON_AddNumberToObject(response, "userLevel", user->privilege);
            cJSON_AddStringToObject(response, "message", message);

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));

            cJSON_Delete(response);
            free(json_str);
            
            free(userRecord);
        }else if(operationCode == SIGNUP_ATTEMPT){
            strcpy(username, json_get_string(json, "username"));
            strcpy(password, json_get_string(json, "password"));

            int status;
            char *message;

            UserRecord* newUserRecord = malloc(sizeof(UserRecord));
            if(user->loggedIn){
                status = FAILURE;
                message = "User already logged in, cant sign up for new account";
            }else if(getUserRecord(username) != NULL){
                message = "Username already exists";
                status = FAILURE;
            }else{
                strcpy(newUserRecord->username, username);
                strcpy(newUserRecord->password, password);
                newUserRecord->privilege = 0;

                if(addUserRecord(newUserRecord) < 0){
                    message = "Failed to create user account";
                    status = FAILURE;
                }else{
                    status = SUCCESS;
                    message = "Signup successful";

                    LeaderboardEntry* leaderboardEntry = malloc(sizeof(LeaderboardEntry));
                    strcpy(leaderboardEntry->username, username);
                    leaderboardEntry->elo = DEFAULT_ELO;
                    addLeaderboardEntry(leaderboardEntry);
                    free(leaderboardEntry);
                }
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            cJSON_AddStringToObject(response, "message", message);

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));

            cJSON_Delete(response);
            free(json_str);

            free(newUserRecord);
        }else if(operationCode == LOGOUT_ATTEMPT){
            int status;
            char *message;
            if(user->loggedIn){
                removeDuelRequest(user->username);
                removeActiveUser(user->username);
                user->loggedIn = 0;
                status = SUCCESS;
                message = "Logout successful";
            }else{
                message = "User not logged in";
                status = FAILURE;
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            cJSON_AddStringToObject(response, "message", message);
            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            cJSON_Delete(response);
            free(json_str);

            if(status == SUCCESS){
                cJSON_Delete(json);
                break;
            }
        }else if(operationCode == ADD_PROBLEM){
            char *title;
            int problemID;
            int difficulty;

            title = json_get_string(json, "title");
            problemID = json_get_int(json, "id");
            difficulty = json_get_int(json, "difficulty");

            Problem* problem = malloc(sizeof(Problem));
            problem->id = problemID;
            strcpy(problem->title, title);
            problem->difficulty = difficulty;
            problem->visibility = PROBLEM_HIDDEN;

            int status = SUCCESS;
            if(addProblem(problem) < 0){
                status = FAILURE;
                free(problem);
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            cJSON_Delete(response);
            free(json_str);

            if(status == FAILURE) continue;

            char tempStatementPath[200];
            char tempInputPath[200];
            char tempOutputPath[200];

            char problemStatementPath[200];
            char problemInputPath[200];
            char problemOutputPath[200];

            sprintf(tempStatementPath, "./tempProblems/%d_statement.txt", problemID);
            sprintf(tempInputPath, "./tempProblems/%d_input.in", problemID);
            sprintf(tempOutputPath, "./tempProblems/%d_output.out", problemID);

            sprintf(problemStatementPath, "./problems/%d_statement.txt", problemID);
            sprintf(problemInputPath, "./problems/%d_input.in", problemID);
            sprintf(problemOutputPath, "./problems/%d_output.out", problemID);

            if(receiveFile(socket, tempStatementPath) < 0){
                deleteProblem(problemID);
                continue;
            }
            if(receiveFile(socket, tempInputPath) < 0){
                deleteProblem(problemID);
                continue;
            }
            if(receiveFile(socket, tempOutputPath) < 0){
                deleteProblem(problemID);
                continue;
            }

            rename(tempStatementPath, problemStatementPath);
            rename(tempInputPath, problemInputPath);
            rename(tempOutputPath, problemOutputPath);

            problem->visibility = PROBLEM_VISIBLE;
            updateProblem(problem);
            free(problem);
        }else if(operationCode == UPDATE_PROBLEM){
            char *title;
            int problemID;
            int difficulty;
            int changeStatement, changeInput, changeOutput;
            int problemVisibility;

            title = json_get_string(json, "title");
            problemID = json_get_int(json, "id");
            difficulty = json_get_int(json, "difficulty");
            changeStatement = json_get_int(json, "changeStatement");
            changeInput = json_get_int(json, "changeInput");
            changeOutput = json_get_int(json, "changeOutput");
            problemVisibility = json_get_int(json, "visibility");

            int fd = open("./database/problems.dat", O_RDWR);
            struct flock lock;
            int index = lockRecord(fd, sizeof(Problem), matchProblem, (void*)&problemID, F_WRLCK, &lock);
            int status = SUCCESS;

            if(index < 0){
                close(fd);
                status = FAILURE;
            }else{
                Problem* problem = (Problem*) getRecordUnlocked(fd, sizeof(Problem), index);
                if(problem == NULL){
                    status = FAILURE;
                }else{
                    strcpy(problem->title, title);
                    problem->difficulty = difficulty;
                    problem->visibility = problemVisibility;
                    if(updateRecordUnlocked(fd, problem, sizeof(Problem), index) < 0){
                        status = FAILURE;
                    }
                    free(problem);
                }
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            cJSON_Delete(response);
            free(json_str);

            if(status == FAILURE) continue;

            char tempStatementPath[200];
            char tempInputPath[200];
            char tempOutputPath[200];

            char problemStatementPath[200];
            char problemInputPath[200];
            char problemOutputPath[200];

            sprintf(tempStatementPath, "./tempProblems/%d_statement.txt", problemID);
            sprintf(tempInputPath, "./tempProblems/%d_input.in", problemID);
            sprintf(tempOutputPath, "./tempProblems/%d_output.out", problemID);

            sprintf(problemStatementPath, "./problems/%d_statement.txt", problemID);
            sprintf(problemInputPath, "./problems/%d_input.in", problemID);
            sprintf(problemOutputPath, "./problems/%d_output.out", problemID);

            if(changeStatement && receiveFile(socket, tempStatementPath) < 0){
                unlockRecord(fd, &lock);
                close(fd);
                continue;
            }

            if(changeInput && receiveFile(socket, tempInputPath) < 0){
                unlockRecord(fd, &lock);
                close(fd);
                continue;
            }

            if(changeOutput && receiveFile(socket, tempOutputPath) < 0){
                unlockRecord(fd, &lock);
                close(fd);
                continue;
            }

            rename(tempStatementPath, problemStatementPath);
            rename(tempInputPath, problemInputPath);
            rename(tempOutputPath, problemOutputPath);

            unlockRecord(fd, &lock);
            close(fd);
        }else if(operationCode == GET_PROBLEM){
            int problemID = json_get_int(json, "id");
            Problem* problem = getProblem(problemID);

            cJSON *response = cJSON_CreateObject();
            int status = SUCCESS;
            if(problem == NULL || (user->privilege == REGULAR_USER && problem->visibility == PROBLEM_HIDDEN)){
                status = FAILURE;
                cJSON_AddNumberToObject(response, "status", FAILURE);
            }else{
                cJSON_AddNumberToObject(response, "status", SUCCESS);
                cJSON_AddStringToObject(response, "title", problem->title);
                cJSON_AddNumberToObject(response, "difficulty", problem->difficulty);
                free(problem);
            }

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
            
            if(status == FAILURE) continue;

            char problemStatementPath[200];
            char problemInputPath[200];
            char problemOutputPath[200];

            sprintf(problemStatementPath, "./problems/%d_statement.txt", problemID);
            sprintf(problemInputPath, "./problems/%d_input.in", problemID);
            sprintf(problemOutputPath, "./problems/%d_output.out", problemID);

            if(sendFile(socket, problemStatementPath, getFileSize(problemStatementPath)) < 0){
                continue;
            }

            if(sendFile(socket, problemInputPath, getFileSize(problemInputPath)) < 0){
                continue;
            }

            if(sendFile(socket, problemOutputPath, getFileSize(problemOutputPath)) < 0){
                continue;
            }
        }else if(operationCode == DELETE_PROBLEM){
            int problemID = json_get_int(json, "id");
            int status = SUCCESS;
            if(deleteProblem(problemID) < 0){
                status = FAILURE;
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);

            if(status == FAILURE) continue;

            char problemStatementPath[200];
            char problemInputPath[200];
            char problemOutputPath[200];

            sprintf(problemStatementPath, "./problems/%d_statement.txt", problemID);
            sprintf(problemInputPath, "./problems/%d_input.in", problemID);
            sprintf(problemOutputPath, "./problems/%d_output.out", problemID);

            deleteFile(problemStatementPath);
            deleteFile(problemInputPath);
            deleteFile(problemOutputPath);
        }else if(operationCode == VIEW_ALL_PROBLEMS){
            int pageNumber = json_get_int(json, "page");

            int actualCount;
            int visibilityFilter = PROBLEM_VISIBLE;
            void* problems;
            int status = SUCCESS;
            
            if((problems = getRecordPage("problems.dat", sizeof(Problem), pageNumber, 3, &actualCount, matchProblemVisibility, (void*)&visibilityFilter)) == NULL){
                status = FAILURE;
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);

            if(status != FAILURE){
                cJSON_AddNumberToObject(response, "count", actualCount);
                cJSON *problemsArray = cJSON_CreateArray();
                for(int i = 0; i < actualCount; i++){   
                    Problem* problem = (Problem*)problems + i;
                    cJSON *problemJSON = cJSON_CreateObject();
                    cJSON_AddNumberToObject(problemJSON, "id", problem->id);
                    cJSON_AddStringToObject(problemJSON, "title", problem->title);
                    cJSON_AddNumberToObject(problemJSON, "difficulty", problem->difficulty);
                    cJSON_AddItemToArray(problemsArray, problemJSON);
                }
                cJSON_AddItemToObject(response, "problems", problemsArray);
                free(problems);
            }

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
        }else if(operationCode == VIEW_A_PROBLEM){
            int problemID = json_get_int(json, "id");
            Problem* problem = getProblem(problemID);

            cJSON *response = cJSON_CreateObject();
            int status = SUCCESS;
            if(problem == NULL || (user->privilege == REGULAR_USER && problem->visibility == PROBLEM_HIDDEN)){
                status = FAILURE;
                cJSON_AddNumberToObject(response, "status", FAILURE);
            }else{
                cJSON_AddNumberToObject(response, "status", SUCCESS);
                cJSON_AddStringToObject(response, "title", problem->title);
                cJSON_AddNumberToObject(response, "difficulty", problem->difficulty);
                free(problem);
            }

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
            
            if(status == FAILURE) continue;

            char problemStatementPath[200];

            sprintf(problemStatementPath, "./problems/%d_statement.txt", problemID);

            if(sendFile(socket, problemStatementPath, getFileSize(problemStatementPath)) < 0){
                continue;
            }
        }else if(operationCode == SUBMIT_SOLUTION){
            int problemID = json_get_int(json, "id");

            char tempSolutionPath[200];
            sprintf(tempSolutionPath, "./tempSolutions/%d_%s_solution.c", problemID, user->username);
            int status = SUCCESS;

            if(getProblem(problemID) == NULL){
                status = FAILURE;
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", SUCCESS);
            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);

            if(status == FAILURE){
                continue;
            }

            if(receiveFile(socket, tempSolutionPath) < 0){
                continue;
            }

            status = SUCCESS;
            int res;
            if((res = submitProblem(problemID, resultBuffer, tempSolutionPath)) < 0){
                status = FAILURE;
            }

            response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);
            cJSON_AddStringToObject(response, "message", resultBuffer);
            json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response); 
        }else if(operationCode == DUEL_USER){
            char *opponentUsername = json_get_string(json, "opponent");

            int ret = processDuelRequest(user->username, opponentUsername);

            cJSON *response = cJSON_CreateObject();
            int status = SUCCESS;
            if(ret == MATCH_FOUND){
                status = GO_TO_DUEL_MENU;
            }else if(ret == ADDED_TO_CHALLENGE_QUEUE){
                status = GO_TO_DUEL_WAITING_MENU;
            }else{
                status = FAILURE;
            }

            cJSON_AddNumberToObject(response, "status", status);
            char* json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);

            if(status == FAILURE) continue;

            if(status == GO_TO_DUEL_MENU){
                printf("0 everything ok till now 3\n");
                int opponentSocket = getUserSocket(opponentUsername);
                printf("0 everything ok till now 4\n");

                Duel* newDuel = malloc(sizeof(Duel));
                newDuel->socket1 = socket;
                newDuel->socket2 = opponentSocket;
                
                printf("0 everything ok till now 5\n");


                strcpy(newDuel->player1, user->username);
                strcpy(newDuel->player2, opponentUsername);

                printf("0 everything ok till now 6\n");


                pthread_mutex_lock(&user->duelLock);
                user->duelRequest = 1;
                pthread_mutex_unlock(&user->duelLock);

                cJSON *bResponse = cJSON_CreateObject();
                cJSON_AddNumberToObject(bResponse, "status", GO_TO_DUEL_MENU);
                char* bJsonStr = cJSON_PrintUnformatted(bResponse);
                write(opponentSocket, bJsonStr, strlen(bJsonStr));
                free(bJsonStr);
                cJSON_Delete(bResponse);

                pthread_t duelThread;
                if(pthread_create(&duelThread, NULL, startDuel, (void*)newDuel) < 0){
                    perror("failed to create duel thread");
                    free(newDuel);
                    continue;
                }
                pthread_detach(duelThread);
            }
        }else if(operationCode == WITHDRAW_CHALLENGE){
            removeDuelRequest(user->username);
        }else if(operationCode == DUEL_ACCEPTED){
            pthread_mutex_lock(&user->duelLock);
            user->duelRequest = 1;
            pthread_mutex_unlock(&user->duelLock);
        }else if(operationCode == VIEW_FULL_LEADERBOARD){
            int pageNumber = json_get_int(json, "page");

            int actualCount;
            void* leaderboardEntries;
            int status = SUCCESS;
            
            if((leaderboardEntries = getRecordPage("leaderboard.dat", sizeof(LeaderboardEntry), pageNumber, 5, &actualCount, NULL, NULL)) == NULL){
                status = FAILURE;
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddNumberToObject(response, "status", status);

            if(status != FAILURE){
                cJSON_AddNumberToObject(response, "count", actualCount);
                cJSON *entriesArray = cJSON_CreateArray();
                for(int i = 0; i < actualCount; i++){   
                    LeaderboardEntry* entry = (LeaderboardEntry*)leaderboardEntries + i;
                    cJSON *entryJSON = cJSON_CreateObject();
                    cJSON_AddStringToObject(entryJSON, "username", entry->username);
                    cJSON_AddNumberToObject(entryJSON, "elo", entry->elo);
                    cJSON_AddItemToArray(entriesArray, entryJSON);
                }
                cJSON_AddItemToObject(response, "entries", entriesArray);
                free(leaderboardEntries);
            }

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
        }else if(operationCode == VIEW_SPECIFIC_ELO){
            char* username = json_get_string(json, "username");

            LeaderboardEntry* entry = getLeaderboardEntry(username);

            cJSON *response = cJSON_CreateObject();
            if(entry == NULL){
                cJSON_AddNumberToObject(response, "status", FAILURE);
            }else{
                cJSON_AddNumberToObject(response, "status", SUCCESS);
                cJSON_AddStringToObject(response, "username", entry->username);
                cJSON_AddNumberToObject(response, "elo", entry->elo);
                free(entry);
            }

            char *json_str = cJSON_PrintUnformatted(response);
            write(socket, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(response);
        }

        cJSON_Delete(json);
    }

    close(socket);
    free(user);
    return NULL;
}

int main(void){
    serverInit();

    int server, new_socket;
    struct sockaddr_in address;
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);

    server = socket(AF_INET, SOCK_STREAM, 0);
    if(server < 0){
        perror("socket failed");
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    int opt = 1;
// Set SO_REUSEADDR before calling bind()
if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt failed");
    return -1;
}


    if(bind(server, (struct sockaddr *)&address, sizeof(address)) < 0){
        perror("bind failed");
        return -1;
    }

    listen(server, 250);
    
    while(1){
        new_socket = accept(server, (struct sockaddr *)&client_address, &client_address_len);
        if(new_socket < 0){
            perror("connection to client failed");
            return -1;
        }

        User* newUser = malloc(sizeof(User));
        newUser->socket = new_socket;
        newUser->loggedIn = 0;

        pthread_t thread_id;

        if(pthread_create(&thread_id, NULL, handleClient, (void*)newUser) < 0){
            perror("failed to create thread");
            free(newUser);
            close(new_socket);
            return -1;
        }
        printf("FOUND SOMEONE\n");
        pthread_detach(thread_id);
    }
}