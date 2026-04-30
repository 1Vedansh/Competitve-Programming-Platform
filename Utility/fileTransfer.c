#include "../Utility/fileTransfer.h"

long long getFileSize(char* filePath){
    struct stat fileStat;
    if(stat(filePath, &fileStat) < 0){
        printf("Failed to get file size of file: %s\n", filePath);
        return -1;
    }

    if(fileStat.st_size > MAX_FILE_SIZE){
        printf("File size must be less than 10MB: %s\n", filePath);
        return -1;
    }

    return fileStat.st_size;
}

int sendFile(int fd, char* filePath, long long fileSize){
    int filefd = open(filePath, O_RDONLY);
    if(filefd < 0){
        printf("Failed to open file: %s\n", filePath);
        return -1;
    }

    if(write(fd, &fileSize, sizeof(long long)) < 0){
        printf("Failed to send file size for file: %s\n", filePath);
        close(filefd);
        return -1;
    }

    char buffer[1024];
    int bytesRead;
    while((bytesRead = read(filefd, buffer, sizeof(buffer))) > 0){
        int totalSent = 0;
        while(totalSent < bytesRead){
            int sent = write(fd, buffer + totalSent, bytesRead - totalSent);
            if(sent < 0){
                printf("Failed to send file data for file: %s\n", filePath);
                close(filefd);
                return -1;
            }
            totalSent += sent;
        }
    }

    close(filefd);
    return 0;
}

int receiveFile(int fd, char* savePath) {
    long long fileSize;
    if (read(fd, &fileSize, sizeof(long long)) < 0){
        printf("Failed to receive file size for file: %s\n", savePath);
        return -1;
    }

    int filefd = open(savePath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (filefd < 0){
        printf("Failed to create file: %s\n", savePath);
        return -1;
    }

    char buffer[1024];
    long long bytesReceived = 0;
    while (bytesReceived < fileSize){
        long long remaining = fileSize - bytesReceived;
        int toRead = (remaining < (long long) sizeof(buffer)) ? (int) remaining : (int) sizeof(buffer);

        int bytesRead = read(fd, buffer, toRead);
        if (bytesRead <= 0){
            printf("Failed to receive file data for file: %s\n", savePath);
            close(filefd);
            return -1;
        }

        int written = 0;
        while (written < bytesRead){
            int w = write(filefd, buffer + written, bytesRead - written);
            if (w < 0){
                printf("Failed to write to file: %s\n", savePath);
                close(filefd);
                return -1;
            }
            written += w;
        }

        bytesReceived += bytesRead;
    }

    close(filefd);
    return 0;
}

int deleteFile(char* filePath){
    if(remove(filePath) < 0){
        printf("Failed to delete file: %s\n", filePath);
        return -1;
    }
    return 0;
}

int readFile(char* filePath){
    int filefd = open(filePath, O_RDONLY);
    if(filefd < 0){
        printf("Failed to open file: %s\n", filePath);
        return -1;
    }

    char buffer[1000];
    int bytesRead;
    while((bytesRead = read(filefd, buffer, sizeof(buffer))) > 0){
        write(1, buffer, bytesRead);
    }
    close(filefd);
    return 0;
}

int readPipe(int pipe_fd, char *buffer, int max_buffer_size, char *prefix){
    if(max_buffer_size <= 0 || buffer == NULL){
        close(pipe_fd);
        return 0;
    }

    int prefix_len = snprintf(buffer, max_buffer_size, "%s", prefix);
    if (prefix_len >= max_buffer_size) prefix_len = max_buffer_size - 1;
    
    int space_left = max_buffer_size - prefix_len - 1;
    int bytesRead = 0;

    if(space_left > 0){
        int totalBytesRead = 0;
    
        while(space_left > 0){
            int bytes = read(pipe_fd, buffer + prefix_len + totalBytesRead, space_left);
            
            if(bytes < 0 && errno == EINTR){
                close(pipe_fd);
                return -1;
            }
            
            if(bytes <= 0){
                break; 
            }
            
            totalBytesRead += bytes;
            space_left -= bytes;
        }

        if(totalBytesRead > 0){
            buffer[prefix_len + totalBytesRead] = '\0';
            bytesRead = totalBytesRead;
        }
    }

    close(pipe_fd); 
    
    return bytesRead > 0 ? bytesRead : 0;
}

int compareFiles(char *file1, char* file2){
    int fd1 = open(file1, O_RDONLY);
    int fd2 = open(file2, O_RDONLY);

    if(fd1 < 0 || fd2 < 0){
        if(fd1 >= 0) close(fd1);
        if(fd2 >= 0) close(fd2);
        return JUDGE_ERROR;
    }

    char c1, c2;
    int bytesRead1, bytesRead2;

    while(1){
        do{
            bytesRead1 = read(fd1, &c1, 1);
        }while(bytesRead1 > 0 && isspace(c1));

        do{
            bytesRead2 = read(fd2, &c2, 1);
        }while(bytesRead2 > 0 && isspace(c2));

        if(bytesRead1 == 0 && bytesRead2 == 0){
            break;
        }

        if(bytesRead1 == 0 || bytesRead2 == 0){
            close(fd1);
            close(fd2);
            return FAILURE;
        }

        if(c1 != c2){
            close(fd1);
            close(fd2);
            return FAILURE;
        }
    }

    close(fd1);
    close(fd2);
    return SUCCESS;
}

int deleteMultipleFiles(int fileCount, ...){
    va_list args;
    va_start(args, fileCount);

    int result = SUCCESS;

    for(int i = 0; i < fileCount; i++){
        char* filePath = va_arg(args, char*);
        if(deleteFile(filePath) < 0){
            result = FAILURE;
        }
    }

    va_end(args);
    return result;
}