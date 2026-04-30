#include "queryDatabase.h"

/* 
    Registered users query handler
*/
int matchUserRecord(void *record, void *targetKey){
    UserRecord* userRecord = (UserRecord*)record;
    char* username = (char*)targetKey;

    return (strcmp(userRecord->username, username) == 0);
}

UserRecord* getUserRecord(char *username){
    return (UserRecord*) getRecord("users.dat", sizeof(UserRecord), matchUserRecord, (void*)username);
}

int addUserRecord(UserRecord* userRecord){
    return insertUniqueRecord("users.dat", (void *)userRecord, sizeof(UserRecord), matchUserRecord, (void*)userRecord->username);
}


/* 
    Problems query handler
*/
int matchProblem(void *record, void *targetKey){
    Problem* problem = (Problem*) record;
    int* id = (int*) targetKey;

    return (problem->id == *id);
}

int matchProblemVisibility(void *record, void *targetKey){
    Problem* problem = (Problem*) record;
    int* visibility = (int*) targetKey;

    return (problem->visibility == *visibility);
}

int addProblem(Problem* problem){
    return insertUniqueRecord("problems.dat", (void *)problem, sizeof(Problem), matchProblem, (void*)&problem->id);
}

int updateProblem(Problem* problem){
    return updateRecord("problems.dat", (void *)problem, sizeof(Problem), matchProblem, (void*)&problem->id);
}

Problem* getProblem(int problemID){
    return (Problem*) getRecord("problems.dat", sizeof(Problem), matchProblem, (void*)&problemID);  
}

int deleteProblem(int problemID){
    return deleteRecord("problems.dat", sizeof(Problem), matchProblem, (void*)&problemID);
}

Problem* getRandomVisibleProblem(){
    int visibilityFilter = PROBLEM_VISIBLE;
    return (Problem*) getRandomRecord("problems.dat", sizeof(Problem), matchProblemVisibility, (void*)&visibilityFilter, 100);
}

/*
    Leaderboard query handler
*/
int matchLeaderboardEntry(void *record, void *targetKey){
    LeaderboardEntry* entry = (LeaderboardEntry*) record;
    char* username = (char*)targetKey;

    return (strcmp(entry->username, username) == 0);
}

int compareLeaderboardEntries(void *record1, void *record2){
    LeaderboardEntry* entry1 = (LeaderboardEntry*) record1;
    LeaderboardEntry* entry2 = (LeaderboardEntry*) record2;

    return entry2->elo - entry1->elo;
}

int addLeaderboardEntry(LeaderboardEntry* entry){
    int ret = insertUniqueRecord("leaderboard.dat", (void *)entry, sizeof(LeaderboardEntry), matchLeaderboardEntry, (void*)entry->username);
    sortLeaderboard();
    return ret;
}

LeaderboardEntry* getLeaderboardEntry(char *username){
    return (LeaderboardEntry*) getRecord("leaderboard.dat", sizeof(LeaderboardEntry), matchLeaderboardEntry, (void*)username);
}

int updateLeaderboardEntry(LeaderboardEntry* entry){
    int ret = updateRecord("leaderboard.dat", (void *)entry, sizeof(LeaderboardEntry), matchLeaderboardEntry, (void*)entry->username);
    sortLeaderboard();
    return ret;
}

int sortLeaderboard(){
    return diskBubbleSortRecords("leaderboard.dat", sizeof(LeaderboardEntry), compareLeaderboardEntries);
}