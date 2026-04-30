#include "genericDatabase.h"

struct flock getFileLock(int lockType, off_t offset, off_t length){
    struct flock lock = {0}; // setting all fields to 0, incase I want to use them later
    lock.l_type = lockType;
    lock.l_whence = SEEK_SET;
    lock.l_start = offset;
    lock.l_len = length;
    return lock;
}

struct flock getReadLock(off_t offset, off_t length){
    return getFileLock(F_RDLCK, offset, length);
}

struct flock getWriteLock(off_t offset, off_t length){
    return getFileLock(F_WRLCK, offset, length);
}

struct flock getUnlock(struct flock lock){
    lock.l_type = F_UNLCK;
    return lock;
}

int getRecordCount(char *filename){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDONLY);
    if(fd < 0){
        perror("failed to open file");
        return -1;
    }

    struct flock lock = getReadLock(0, sizeof(int));

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire read lock");
        close(fd);
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release read lock");
        close(fd);
        return -1;
    }

    close(fd);
    return recordCount;
}

void* getRecord(char *filename, int recordSize, MatchFunction matchFunction, void *targetKey){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDONLY);
    if(fd < 0){
        perror("failed to open file");
        return NULL;
    }

    struct flock lock = getReadLock(0,0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire read lock");
        close(fd);
        return NULL;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    void *record = NULL;
    for(int i = 0; i < recordCount; i++){
        void *currentRecord = getRecordUnlocked(fd, recordSize, i);
        if(currentRecord == NULL) continue;

        if(matchFunction(currentRecord, targetKey) == 1){
            record = currentRecord;
            break;
        }
        free(currentRecord);
    }

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release read lock");
        free(record);
        close(fd);
        return NULL;
    }

    close(fd);
    return record;
}

int insertRecord(char *filename, void *record, int recordSize){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDWR);
    if(fd < 0){
        perror("failed to open file");
        return -1;
    }

    struct flock lock = getWriteLock(0, 0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire write lock");
        close(fd);
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    lseek(fd, sizeof(int) + recordCount * recordSize, SEEK_SET);
    write(fd, record, recordSize);

    recordCount++;
    lseek(fd, 0, SEEK_SET);
    write(fd, &recordCount, sizeof(int));

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release write lock");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int updateRecord(char *filename, void *newRecord, int recordSize, MatchFunction matchFunction, void *targetKey){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDWR);
    if(fd < 0){
        perror("failed to open file");
        return -1;
    }

    struct flock lock = getWriteLock(0,0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire write lock");
        close(fd);
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    int success = -1;

    for(int i = 0; i < recordCount; i++){
        void *currentRecord = getRecordUnlocked(fd, recordSize, i);
        if(currentRecord == NULL) continue;

        if(matchFunction(currentRecord, targetKey) == 1){
            success = updateRecordUnlocked(fd, newRecord, recordSize, i);
            free(currentRecord);
            break;
        }
        free(currentRecord);
    }

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release write lock");
        close(fd);
        return -1;
    }

    close(fd);
    return success;
}

int deleteRecord(char *filename, int recordSize, MatchFunction matchFunction, void *targetKey){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDWR);
    if(fd < 0){
        perror("failed to open file");
        return -1;
    }

    struct flock lock = getWriteLock(0, 0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire write lock");
        close(fd);
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    int targetIndex = -1;
    for(int i = 0; i < recordCount; i++){
        void *currentRecord = getRecordUnlocked(fd, recordSize, i);
        if(currentRecord == NULL) continue;

        if(matchFunction(currentRecord, targetKey) == 1){
            targetIndex = i;
            free(currentRecord);
            break;
        }
        free(currentRecord);
    }

    if(targetIndex != -1){
        void *lastRecord = malloc(recordSize);
        lseek(fd, sizeof(int) + (recordCount - 1) * recordSize, SEEK_SET);
        read(fd, lastRecord, recordSize);

        lseek(fd, sizeof(int) + targetIndex * recordSize, SEEK_SET);
        write(fd, lastRecord, recordSize);
        free(lastRecord);

        recordCount--;
        lseek(fd, 0, SEEK_SET);
        write(fd, &recordCount, sizeof(int));
    }

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release write lock");
        close(fd);
        return -1;
    }

    close(fd);
    return (targetIndex != -1) ? 0 : -1;
}

int insertUniqueRecord(char *filename, void *record, int recordSize, MatchFunction matchFunction, void *targetKey){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDWR);
    if(fd < 0){
        perror("failed to open file");
        return -1;
    }

    struct flock lock = getWriteLock(0, 0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire write lock");
        close(fd);
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    for(int i = 0; i < recordCount; i++){
        void *currentRecord = getRecordUnlocked(fd, recordSize, i);
        if(currentRecord == NULL) continue;

        if(matchFunction(currentRecord, targetKey) == 1){
            free(currentRecord);
            lock = getUnlock(lock);
            fcntl(fd, F_SETLK, &lock);
            close(fd);
            return -1;
        }
        free(currentRecord);
    }

    lseek(fd, sizeof(int) + recordCount * recordSize, SEEK_SET);
    write(fd, record, recordSize);

    recordCount++;
    lseek(fd, 0, SEEK_SET);
    write(fd, &recordCount, sizeof(int));

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release write lock");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int lockRecord(int fd, int recordSize, MatchFunction MatchFunction, void *targetKey, int lockType, struct flock *lock){
    *lock = getFileLock(lockType, 0, 0);

    if(fcntl(fd, F_SETLKW, lock) < 0){
        perror("failed to acquire lock");
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    int recordIndex = -1;
    for(int i = 0; i < recordCount; i++){
        void *currentRecord = getRecordUnlocked(fd, recordSize, i);
        if(currentRecord == NULL) continue;

        if(MatchFunction(currentRecord, targetKey) == 1){
            recordIndex = i;
            free(currentRecord);
            break;
        }
        free(currentRecord);
    }

    if(recordIndex == -1){
        *lock = getUnlock(*lock);
        fcntl(fd, F_SETLK, lock);
    }

    return recordIndex;
}

int unlockRecord(int fd, struct flock *lock){
    *lock = getUnlock(*lock);
    if(fcntl(fd, F_SETLK, lock) < 0){
        perror("failed to release lock");
        return -1;
    }
    return 0;
}

void* getRecordUnlocked(int fd, int recordSize, int recordIndex){
    void *record = malloc(recordSize);
    lseek(fd, sizeof(int) + recordIndex * recordSize, SEEK_SET);
    read(fd, record, recordSize);

    return record;
}

int updateRecordUnlocked(int fd, void *record, int recordSize, int recordIndex){
    lseek(fd, sizeof(int) + recordIndex * recordSize, SEEK_SET);
    write(fd, record, recordSize);

    return 0;
}

void* getRecordPage(char *filename, int recordSize, int pageNumber, int recordsPerPage, int *returnedCount, MatchFunction matchFunction, void *targetKey){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDONLY);
    if(fd < 0){
        perror("failed to open file");
        return NULL;
    }

    struct flock lock = getReadLock(0,0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire read lock");
        close(fd);
        return NULL;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    int startIndex = (pageNumber - 1) * recordsPerPage;

    void *records = malloc(recordSize * recordsPerPage);
    int actualCount = 0;

    while(actualCount < recordsPerPage && startIndex < recordCount){
        void *currentRecord = getRecordUnlocked(fd, recordSize, startIndex);
        if(currentRecord == NULL){
            startIndex++;
            continue;
        }

        if(matchFunction == NULL || matchFunction(currentRecord, targetKey) == 1){
            memcpy((char*)records + actualCount * recordSize, currentRecord, recordSize);
            actualCount++;
        }
        free(currentRecord);
        startIndex++;
    }

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release read lock");
        free(records);
        close(fd);
        return NULL;
    }

    close(fd);
    *returnedCount = actualCount;
    return records;
}

void* getRandomRecord(char *filename, int recordSize, MatchFunction matchFunction, void *targetKey, int maxTries){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDONLY);
    if(fd < 0){
        perror("failed to open file");
        return NULL;
    }

    struct flock lock = getReadLock(0,0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire read lock");
        close(fd);
        return NULL;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    if(recordCount == 0){
        lock = getUnlock(lock);
        fcntl(fd, F_SETLK, &lock);
        close(fd);
        return NULL;
    }

    void *record = NULL;
    srand(time(NULL));
    int startIndex = rand() % recordCount;
    int tries = 0;

    while(tries < maxTries){
        record = getRecordUnlocked(fd, recordSize, startIndex);
        if(record == NULL){
            startIndex = rand() % recordCount;
            continue;
        }

        if(matchFunction == NULL || matchFunction(record, targetKey) == 1){
            break;
        }

        free(record);
        startIndex =rand() % recordCount;
        tries++;
    }

    if(tries == maxTries){
        record = NULL;
    }

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release read lock");
        free(record);
        close(fd);
        return NULL;
    }

    close(fd);
    return record;
}

// Decreasing order
int diskBubbleSortRecords(char *filename, int recordSize, MatchFunction compareFunction){
    char filePath[100];
    strcpy(filePath, DATABASE_PATH);
    strcat(filePath, filename);

    int fd = open(filePath, O_RDWR);
    if(fd < 0){
        perror("failed to open file");
        return -1;
    }

    struct flock lock = getWriteLock(0, 0);

    if(fcntl(fd, F_SETLKW, &lock) < 0){
        perror("failed to acquire write lock");
        close(fd);
        return -1;
    }

    int recordCount;
    read(fd, &recordCount, sizeof(int));

    void *records = malloc(recordSize * recordCount);
    read(fd, records, recordSize * recordCount);

    for(int i = 0; i < recordCount - 1; i++){
        for(int j = 0; j < recordCount - i - 1; j++){
            void *recordA = (char*)records + j * recordSize;
            void *recordB = (char*)records + (j + 1) * recordSize;

            if(compareFunction(recordA, recordB) < 0){
                char temp[recordSize];
                memcpy(temp, recordA, recordSize);
                memcpy(recordA, recordB, recordSize);
                memcpy(recordB, temp, recordSize);
            }
        }
    }

    lseek(fd, sizeof(int), SEEK_SET);
    write(fd, records, recordSize * recordCount);

    free(records);

    lock = getUnlock(lock);
    if(fcntl(fd, F_SETLK, &lock) < 0){
        perror("failed to release write lock");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}