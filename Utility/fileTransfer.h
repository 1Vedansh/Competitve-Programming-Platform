#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H


#define MAX_FILE_SIZE 10000000 //10MB

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <aio.h>
#include <stdarg.h>

#include "../Utility/codes.h"

long long getFileSize(char* filePath);
int sendFile(int fd, char* filePath, long long fileSize);
int receiveFile(int fd, char* savePath);
int deleteFile(char* filePath);
int readFile(char* filePath);
int readPipe(int pipe_fd, char *buffer, int max_buffer_size, char *prefix);
int compareFiles(char *file1, char* file2);
int deleteMultipleFiles(int fileCount, ...);

#endif