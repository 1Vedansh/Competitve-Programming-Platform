#ifndef GENERIC_DATABASE_H
#define GENERIC_DATABASE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define DATABASE_PATH "./database/"

typedef int (*MatchFunction)(void *record, void *targetKey);

struct flock getFileLock(int lockType, off_t offset, off_t length);
struct flock getReadLock(off_t offset, off_t length);
struct flock getWriteLock(off_t offset, off_t length);
struct flock getUnlock(struct flock lock);

int getRecordCount(char *filename);
void* getRecord(char *filename, int recordSize, MatchFunction matchFunction, void *targetKey);
int insertRecord(char *filename, void *record, int recordSize);
int updateRecord(char *filename, void *newRecord, int recordSize, MatchFunction matchFunction, void *targetKey);
int deleteRecord(char *filename, int recordSize, MatchFunction matchFunction, void *targetKey);
int insertUniqueRecord(char *filename, void *record, int recordSize, MatchFunction matchFunction, void *targetKey);
int lockRecord(int fd, int recordSize, MatchFunction MatchFunction, void *targetKey, int lockType, struct flock *lock);
int unlockRecord(int fd, struct flock *lock);

void* getRecordUnlocked(int fd, int recordSize, int recordIndex);
int updateRecordUnlocked(int fd, void *record, int recordSize, int recordIndex);

void* getRecordPage(char *filename, int recordSize, int pageNumber, int recordsPerPage, int *returnedCount, MatchFunction matchFunction, void *targetKey);
void* getRandomRecord(char *filename, int recordSize, MatchFunction matchFunction, void *targetKey, int maxTries);
int diskBubbleSortRecords(char *filename, int recordSize, MatchFunction compareFunction);

#endif