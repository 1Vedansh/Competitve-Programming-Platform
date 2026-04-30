#ifndef STRUCTS_H
#define STRUCTS_H

#include <pthread.h>

typedef struct{
    char username[100];
    int loggedIn;     // 0 = not logged in, 1 = logged in successfully
    int socket; // socket that the server uses to communicate with the client
    int privilege; // 0 = regular user, 1 = admin
    pthread_mutex_t duelLock;
    pthread_cond_t duelCond;
    int duelRequest; // 0 = not in duel, 1 = in a duel
}User;

typedef struct{
    char username[100];
    char password[100];
    int privilege; // 0 = regular user, 1 = admin
}UserRecord;

typedef struct{
    int id;
    char title[100];
    int difficulty; // 0 = easy, 1 = medium, 2 = hard
    int visibility;
}Problem;

typedef struct{
    char challenger[100];
    char target[100];
}Challenge;

typedef struct{
    int socket1;
    int socket2;
    char player1[100];
    char player2[100];
}Duel;

typedef struct{
    char username[100];
    int elo;
}LeaderboardEntry;

#endif