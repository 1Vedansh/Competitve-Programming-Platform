#ifndef QUERY_DATABASE_H
#define QUERY_DATABASE_H

#include "../Utility/structs.h"
#include "../Utility/codes.h"
#include "genericDatabase.h"

int matchUserRecord(void *record, void *targetKey);
UserRecord* getUserRecord(char *username);
int addUserRecord(UserRecord* userRecord);

int matchProblem(void *record, void *targetKey);
int matchProblemVisibility(void *record, void *targetKey);
int addProblem(Problem* problem);
int updateProblem(Problem* problem);
Problem* getProblem(int problemID);
int deleteProblem(int problemID);
Problem* getRandomVisibleProblem();

int matchLeaderboardEntry(void *record, void *targetKey);
int compareLeaderboardEntries(void *record1, void *record2);
int addLeaderboardEntry(LeaderboardEntry* entry);
LeaderboardEntry* getLeaderboardEntry(char *username);
int updateLeaderboardEntry(LeaderboardEntry* entry);
int sortLeaderboard();

#endif
