#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/select.h>

#include "../Utility/codes.h"
#include "../Utility/cJSON.h"
#include "../Utility/jsonUtility.h"
#include "../Utility/fileTransfer.h"

void clearScreen() {
    printf("\e[1;1H\e[2J");
}

// #define ONLINE_VM

#include <netdb.h>
in_addr_t resolve_ipv4(const char *hostname) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0){
        return INADDR_NONE; 
    }

    
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
    in_addr_t ip_addr = ipv4->sin_addr.s_addr;

    freeaddrinfo(res);
    return ip_addr;
}

typedef struct{
    char *label;
    int operation;
}MenuItem;

MenuItem loginMenu[] = {
    {"Login", LOGIN_ATTEMPT},
    {"Signup", SIGNUP_ATTEMPT},
    {"Logout", LOGOUT_ATTEMPT},
    {NULL, -1}
};

MenuItem adminMenu[] = {
    {"Modify problems", GO_TO_MODIFY_PROBLEMS_MENU},
    {"Go back", GO_TO_LOGIN_MENU},
    {NULL, -1}
};

MenuItem userMenu[] = {
    {"View all problems", VIEW_ALL_PROBLEMS},
    {"View a Problem", VIEW_A_PROBLEM},
    {"Submit Solution", SUBMIT_SOLUTION},
    {"Fight a user", DUEL_USER},
    {"Go back", GO_TO_LOGIN_MENU},
    {"View full leaderboard", VIEW_FULL_LEADERBOARD},
    {"View specific ELO", VIEW_SPECIFIC_ELO},
    {NULL, -1}
};

MenuItem modifyProblemsMenu[] = {
    {"Add problem", ADD_PROBLEM},
    {"Update problem", UPDATE_PROBLEM},
    {"Get problem", GET_PROBLEM},
    {"Delete problem", DELETE_PROBLEM},
    {"Go back", GO_TO_ADMIN_MENU},
    {NULL, -1}
};

MenuItem duelMenu[] = {
    {"View problem statement", VIEW_DUEL_PROBLEM_STATEMENT},
    {"Submit solution", SUBMIT_DUEL_SOLUTION},
    {"Forfeit", FORFEIT_DUEL},
    {NULL, -1}
};

void stripNewline(char *text){
    if(text == NULL){
        return;
    }

    size_t length = strlen(text);
    if(length > 0 && text[length - 1] == '\n'){
        text[length - 1] = '\0';
    }
}

void handleDuelMenu(int fd, int *currentMenu){
    char socketBuffer[1000];
    char inputBuffer[1000];

    while(*currentMenu == DUEL_MENU){
        for(int i = 0; duelMenu[i].label != NULL; i++){
            printf("%d. %s\n", i + 1, duelMenu[i].label);
        }
        printf("\nEnter your choice: ");
        fflush(stdout);

        int userChoice;
        scanf("%d", &userChoice);

        // if(fgets(inputBuffer, sizeof(inputBuffer), stdin) == NULL){
        //     return;
        // }

        // if(inputBuffer[0] == '\n' || inputBuffer[0] == '\0'){
        //     continue;
        // }

        // int choice = atoi(inputBuffer);

        int choice = duelMenu[userChoice - 1].operation;

        if(choice == VIEW_DUEL_PROBLEM_STATEMENT){
            cJSON *request = cJSON_CreateObject();
            cJSON_AddNumberToObject(request, "op", VIEW_DUEL_PROBLEM_STATEMENT);

            char *json_str = cJSON_PrintUnformatted(request);
            write(fd, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(request);

            memset(socketBuffer, 0, sizeof(socketBuffer));
            int bytesReadMsg = read(fd, socketBuffer, sizeof(socketBuffer));
            cJSON *response = cJSON_Parse(socketBuffer);

            int status = json_get_int(response, "status");
            if(status == GO_TO_USER_MENU){
                char *message = json_get_string(response, "message");
                if(message != NULL){
                    printf("\n%s\n", message);
                }
                cJSON_Delete(response);
                *currentMenu = USER_MENU;
                return;
            }

            if(status != SUCCESS){
                char *message = json_get_string(response, "message");
                if(message != NULL){
                    printf("%s\n", message);
                }
                cJSON_Delete(response);
                continue;
            }

            char *title = json_get_string(response, "title");
            int difficulty = json_get_int(response, "difficulty");
            if(title != NULL){
                printf("Title: %s, Difficulty: %d\n", title, difficulty);
            }
            cJSON_Delete(response);

            char tempProblemStatementPath[200];
            strcpy(tempProblemStatementPath, "./tempDuelProblemStatement.txt");

            if(receiveFile(fd, tempProblemStatementPath) < 0){
                printf("Failed to receive duel problem statement.\n");
                break;
            }

            printf("Problem Statement:\n");
            readFile(tempProblemStatementPath);
            deleteFile(tempProblemStatementPath);
        }else if(choice == SUBMIT_DUEL_SOLUTION){
            char solutionPath[200];
            printf("Give absolute path to your solution file(should be C code): ");
            // if(fgets(solutionPath, sizeof(solutionPath), stdin) == NULL){
            //     continue;
            // }
            // stripNewline(solutionPath);

            scanf("%s", solutionPath);

            long long fileSize = getFileSize(solutionPath);
            if(fileSize < 0){
                continue;
            }

            cJSON *request = cJSON_CreateObject();
            cJSON_AddNumberToObject(request, "op", SUBMIT_DUEL_SOLUTION);

            char *json_str = cJSON_PrintUnformatted(request);
            write(fd, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(request);

            memset(socketBuffer, 0, sizeof(socketBuffer));
            int bytesReadMsg2 = read(fd, socketBuffer, sizeof(socketBuffer));
            cJSON *response = cJSON_Parse(socketBuffer);

            int status = json_get_int(response, "status");
            char *message = json_get_string(response, "message");
            if(message != NULL){
                printf("%s\n", message);
            }
            cJSON_Delete(response);

            if(status == GO_TO_USER_MENU){
                *currentMenu = USER_MENU;
                return;
            }

            if(status != SUCCESS){
                continue;
            }

            if(sendFile(fd, solutionPath, fileSize) < 0){
                printf("Failed to send duel solution.\n");
                continue;
            }

            memset(socketBuffer, 0, sizeof(socketBuffer));
            int bytesReadMsg3 = read(fd, socketBuffer, sizeof(socketBuffer) - 1);
            if(bytesReadMsg3 > 0) socketBuffer[bytesReadMsg3] = '\0';
            response = cJSON_Parse(socketBuffer);

            status = json_get_int(response, "status");
            message = json_get_string(response, "message");
            if(message != NULL){
                printf("%s\n", message);
            }
            cJSON_Delete(response);

            if(status == GO_TO_USER_MENU){
                *currentMenu = USER_MENU;
                return;
            }
        }else if(choice == FORFEIT_DUEL){
            cJSON *request = cJSON_CreateObject();
            cJSON_AddNumberToObject(request, "op", FORFEIT_DUEL);

            char *json_str = cJSON_PrintUnformatted(request);
            write(fd, json_str, strlen(json_str));
            free(json_str);
            cJSON_Delete(request);

            memset(socketBuffer, 0, sizeof(socketBuffer));
            int bytesReadMsg4 = read(fd, socketBuffer, sizeof(socketBuffer));
            cJSON *response = cJSON_Parse(socketBuffer);

            int status = json_get_int(response, "status");
            char *message = json_get_string(response, "message");
            if(message != NULL){
                printf("%s\n", message);
            }
            cJSON_Delete(response);

            if(status == GO_TO_USER_MENU){
                *currentMenu = USER_MENU;
                return;
            }
        }else{
            printf("Invalid choice, please try again.\n");
        }
    }
}

int main(){
    int fd;
    struct sockaddr_in serverAddress;
    char buffer[1000];

    if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return -1;
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);

    #ifdef ONLINE_VM
        const char *azure_dns = "competitive.eastasia.cloudapp.azure.com";
        in_addr_t target_ip = resolve_ipv4(azure_dns);

        if (target_ip == INADDR_NONE) {
            printf("DNS Resolution failed!\n");
            return 1;
        }

        serverAddress.sin_addr.s_addr = target_ip;
    #else
        serverAddress.sin_addr.s_addr = INADDR_ANY;
    #endif

    
    if(connect(fd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        printf("Connection Failed\n");
        return -1;
    }

    printf("Successfully connected to the server!\n");

    int currentMenu = LOGIN_MENU;
    while(1){
        char username[100];
        char permanentUsername[100];
        char password[100];
        char title[100];
        char problemStatementPath[200];
        char problemInputPath[200];
        char problemOutputPath[200];
        int problemID;
        int problemDifficulty;
        long long fileSize1,fileSize2,fileSize3;
        int problemVisibility;
        int pageNumber;
        int status;
        char *message;

        MenuItem *currentItems;
        if(currentMenu == LOGIN_MENU){
            currentItems = loginMenu;
        }else if(currentMenu == ADMIN_MENU){
            currentItems = adminMenu;
        }else if(currentMenu == USER_MENU){
            currentItems = userMenu;
        }else if(currentMenu == MODIFY_PROBLEMS_MENU){
            currentItems = modifyProblemsMenu;
        }else if(currentMenu == DUEL_MENU){
            currentItems = duelMenu;
        }else{
            printf("Invalid menu state\n");
            break;
        }

        if(currentMenu == DUEL_MENU){
            handleDuelMenu(fd, &currentMenu);
            memset(buffer, 0, sizeof(buffer));
            continue;
        }

        int count = 0;
        while(currentItems[count].label != NULL){
            printf("%d. %s\n", count + 1, currentItems[count].label);
            count++;
        }

        int userChoice;
        printf("\nEnter your choice: ");
        scanf("%d", &userChoice);

        if(userChoice < 1 || userChoice > count){
            printf("Invalid choice, please try again.\n");
            continue;
        }

        int operationCode = currentItems[userChoice - 1].operation;

        cJSON *request;
        cJSON *json;
        char *json_str;
        int bytesRead;

        switch (operationCode){
            case LOGIN_ATTEMPT:
                printf("Enter username: ");
                scanf("%s", username);
                printf("Enter password: ");
                scanf("%s", password);

                request = cJSON_CreateObject();
                cJSON_AddNumberToObject(request, "op", LOGIN_ATTEMPT);
                cJSON_AddStringToObject(request, "username", username);
                cJSON_AddStringToObject(request, "password", password);

                json_str = cJSON_PrintUnformatted(request);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(request);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead > 0){
                    json = cJSON_Parse(buffer);

                    status = json_get_int(json, "status");
                    int user_level = json_get_int(json, "userLevel");
                    message = json_get_string(json, "message");

                    printf("Status: %d, User Level: %d, Message: %s\n", status, user_level, message);
                    cJSON_Delete(json);
                    if(status == SUCCESS){
                        if(user_level == ADMIN_USER){
                            currentMenu = ADMIN_MENU;
                        }else if(user_level == REGULAR_USER){
                            currentMenu = USER_MENU;
                        }
                        strcpy(permanentUsername, username);
                    }
                }else{
                    printf("No response from server\n");
                }
                break;
            case SIGNUP_ATTEMPT:
                printf("Enter username: ");
                scanf("%s", username);
                printf("Enter password: ");
                scanf("%s", password);

                request = cJSON_CreateObject();
                cJSON_AddNumberToObject(request, "op", SIGNUP_ATTEMPT);
                cJSON_AddStringToObject(request, "username", username);
                cJSON_AddStringToObject(request, "password", password);

                json_str = cJSON_PrintUnformatted(request);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(request);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead > 0){
                    json = cJSON_Parse(buffer);

                    status = json_get_int(json, "status");
                    message = json_get_string(json, "message");

                    printf("Status: %d, Message: %s\n", status, message);
                    cJSON_Delete(json);
                }else{
                    printf("No response from server\n");
                }
                break;
            case LOGOUT_ATTEMPT:
                request = cJSON_CreateObject();
                cJSON_AddNumberToObject(request, "op", LOGOUT_ATTEMPT);

                json_str = cJSON_PrintUnformatted(request);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(request);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead > 0){
                    json = cJSON_Parse(buffer);

                    int status = json_get_int(json, "status");
                    message = json_get_string(json, "message");

                    printf("Status: %d, Message: %s\n", status, message);
                    cJSON_Delete(json);

                    if(status == SUCCESS){
                        close(fd);
                        return 0;
                    }
                }else{
                    printf("No response from server\n");
                }
                break;
            case ADD_PROBLEM:
                printf("Enter problem title(No spaces): ");
                scanf("%s", title);
                printf("Enter problem ID: ");
                scanf("%d", &problemID);
                printf("Enter problem difficulty (%d = easy, %d = medium, %d = hard): ", EASY_PROBLEM, MEDIUM_PROBLEM, HARD_PROBLEM);
                scanf("%d", &problemDifficulty);
                printf("Give absolute path to the problem statement file: ");
                scanf("%s", problemStatementPath);
                printf("Give absolute path to the problem input file: ");
                scanf("%s", problemInputPath);
                printf("Give absolute path to the problem output file: ");
                scanf("%s", problemOutputPath);

                if((fileSize1 = getFileSize(problemStatementPath)) < 0){
                    break;
                }
                if((fileSize2 = getFileSize(problemInputPath)) < 0){
                    break;
                }
                if((fileSize3 = getFileSize(problemOutputPath)) < 0){
                    break;
                }

                cJSON *problemData = cJSON_CreateObject();
                cJSON_AddNumberToObject(problemData, "op", ADD_PROBLEM);
                cJSON_AddStringToObject(problemData, "title", title);
                cJSON_AddNumberToObject(problemData, "id", problemID);
                cJSON_AddNumberToObject(problemData, "difficulty", problemDifficulty);

                json_str = cJSON_PrintUnformatted(problemData);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(problemData);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead > 0){
                    json = cJSON_Parse(buffer);
                    int status = json_get_int(json, "status");
                    if(status != SUCCESS){
                        printf("Failed to add problem\n");
                        cJSON_Delete(json);
                        break;
                    }
                }else{
                    printf("No response from server\n");
                    break;
                }

                if(sendFile(fd, problemStatementPath, fileSize1) < 0){
                    break;
                }

                if(sendFile(fd, problemInputPath, fileSize2) < 0){
                    break;
                }

                if(sendFile(fd, problemOutputPath, fileSize3) < 0){
                    break;
                }

                printf("Problem added successfully!\n");
                break;
            case UPDATE_PROBLEM:
                printf("Enter problem ID: ");
                scanf("%d", &problemID);
                printf("Enter new problem title(No spaces): ");
                scanf("%s", title);
                printf("Enter new problem difficulty (%d = easy, %d = medium, %d = hard): ", EASY_PROBLEM, MEDIUM_PROBLEM, HARD_PROBLEM);
                scanf("%d", &problemDifficulty);
                printf("Is the problem visible to users? (%d for yes, %d for no): ", PROBLEM_VISIBLE, PROBLEM_HIDDEN);
                scanf("%d", &problemVisibility);

                int changeStatement = 0, changeInput = 0, changeOutput = 0;

                printf("Do you want to change the problem statement? (1 for yes, 0 for no): ");
                scanf("%d", &changeStatement);
                printf("Do you want to change the problem input? (1 for yes, 0 for no): ");
                scanf("%d", &changeInput);
                printf("Do you want to change the problem output? (1 for yes, 0 for no): ");
                scanf("%d", &changeOutput);

                if(changeStatement){
                    printf("Give absolute path to the new problem statement file: ");
                    scanf("%s", problemStatementPath);
                }
                if(changeInput){
                    printf("Give absolute path to the new problem input file: ");
                    scanf("%s", problemInputPath);
                }
                if(changeOutput){
                    printf("Give absolute path to the new problem output file: ");
                    scanf("%s", problemOutputPath);
                }

                if(changeStatement && ((fileSize1 = getFileSize(problemStatementPath)) < 0)){
                    break;
                }
                if(changeInput && ((fileSize2 = getFileSize(problemInputPath)) < 0)){
                    break;
                }
                if(changeOutput &&  ((fileSize3 = getFileSize(problemOutputPath)) < 0)){
                    break;
                }

                cJSON *updateData = cJSON_CreateObject();
                cJSON_AddNumberToObject(updateData, "op", UPDATE_PROBLEM);
                cJSON_AddStringToObject(updateData, "title", title);
                cJSON_AddNumberToObject(updateData, "id", problemID);
                cJSON_AddNumberToObject(updateData, "difficulty", problemDifficulty);
                cJSON_AddNumberToObject(updateData, "changeStatement", changeStatement);
                cJSON_AddNumberToObject(updateData, "changeInput", changeInput);
                cJSON_AddNumberToObject(updateData, "changeOutput", changeOutput);
                cJSON_AddNumberToObject(updateData, "visibility", problemVisibility);

                json_str = cJSON_PrintUnformatted(updateData);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(updateData);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead > 0){
                    json = cJSON_Parse(buffer);
                    int status = json_get_int(json, "status");
                    if(status != SUCCESS){
                        printf("Failed to update problem\n");
                        cJSON_Delete(json);
                        break;
                    }
                }else{
                    printf("No response from server\n");
                    break;
                }

                if(changeStatement && sendFile(fd, problemStatementPath, fileSize1) < 0){
                    break;
                }

                if(changeInput && sendFile(fd, problemInputPath, fileSize2) < 0){
                    break;
                }  

                if(changeOutput && sendFile(fd, problemOutputPath, fileSize3) < 0){
                    break;
                }

                printf("Problem updated successfully!\n");
                break;
            case GET_PROBLEM:
                printf("Enter problem ID: ");
                scanf("%d", &problemID);

                cJSON *getData = cJSON_CreateObject();
                cJSON_AddNumberToObject(getData, "op", GET_PROBLEM);
                cJSON_AddNumberToObject(getData, "id", problemID);

                json_str = cJSON_PrintUnformatted(getData);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(getData);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead <= 0){
                    printf("No response from server\n");
                    break;
                }

                json = cJSON_Parse(buffer);
                status = json_get_int(json, "status");
                if(status != SUCCESS){
                    printf("Failed to get problem\n");
                    cJSON_Delete(json);
                    break;
                }
                char *title = json_get_string(json, "title");
                int difficulty = json_get_int(json, "difficulty");

                printf("Title: %s, Difficulty: %d\n", title, difficulty);

                char problemStatementPath[200];
                char problemInputPath[200];
                char problemOutputPath[200];

                strcpy(problemStatementPath, "./tempProblemStatement.txt");
                strcpy(problemInputPath, "./tempProblemInput.in");
                strcpy(problemOutputPath, "./tempProblemOutput.out");

                if(receiveFile(fd, problemStatementPath) < 0){
                    break;
                }

                if(receiveFile(fd, problemInputPath) < 0){
                    break;
                }

                if(receiveFile(fd, problemOutputPath) < 0){
                    break;
                }

                printf("Problem files received successfully!\n");
                printf("Problem Statement saved at: %s\n", problemStatementPath);
                printf("Problem Input saved at: %s\n", problemInputPath);
                printf("Problem Output saved at: %s\n", problemOutputPath);
                
                cJSON_Delete(json);
                break;
            case DELETE_PROBLEM:
                printf("Enter problem ID: ");
                scanf("%d", &problemID);

                cJSON *deleteData = cJSON_CreateObject();
                cJSON_AddNumberToObject(deleteData, "op", DELETE_PROBLEM);
                cJSON_AddNumberToObject(deleteData, "id", problemID);

                json_str = cJSON_PrintUnformatted(deleteData);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(deleteData);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead > 0){
                    json = cJSON_Parse(buffer);
                    int status = json_get_int(json, "status");
                    if(status == SUCCESS){
                        printf("Problem deleted successfully!\n");
                    }else{
                        printf("Failed to delete problem\n");
                    }
                    cJSON_Delete(json);
                }else{
                    printf("No response from server\n");
                }
                break;
            case VIEW_ALL_PROBLEMS:
            {
                printf("Enter page number: ");
                scanf("%d", &pageNumber);

                cJSON *request = cJSON_CreateObject();
                cJSON_AddNumberToObject(request, "op", VIEW_ALL_PROBLEMS);
                cJSON_AddNumberToObject(request, "page", pageNumber);

                char *json_str = cJSON_PrintUnformatted(request);
                write(fd, json_str, strlen(json_str));
                free(json_str);
                cJSON_Delete(request);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead <= 0){
                    printf("No response from server\n");
                    break;
                }

                cJSON *response = cJSON_Parse(buffer);
                status = json_get_int(response, "status");
                if(status != SUCCESS){
                    printf("Failed to retrieve problems\n");
                    cJSON_Delete(response);
                    break;
                }
                int count = json_get_int(response, "count");
                cJSON *problemsArray = cJSON_GetObjectItem(response, "problems");
                printf("Problems on page %d:\n", pageNumber);
                for(int i = 0; i < count; i++){
                    cJSON *problemJSON = cJSON_GetArrayItem(problemsArray, i);
                    int id = json_get_int(problemJSON, "id");
                    char *title = json_get_string(problemJSON, "title");
                    int difficulty = json_get_int(problemJSON, "difficulty");
                    printf("ID: %d, Title: %s, Difficulty: %d\n", id, title, difficulty);
                }
                cJSON_Delete(response);

                break;
            }
            case VIEW_A_PROBLEM:
                printf("Enter problem ID: ");
                scanf("%d", &problemID);

                cJSON *getProblemData = cJSON_CreateObject();
                cJSON_AddNumberToObject(getProblemData, "op", VIEW_A_PROBLEM);
                cJSON_AddNumberToObject(getProblemData, "id", problemID);

                json_str = cJSON_PrintUnformatted(getProblemData);
                write(fd, json_str, strlen(json_str));

                free(json_str);
                cJSON_Delete(getProblemData);

                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead <= 0){
                    printf("No response from server\n");
                    break;
                }

                json = cJSON_Parse(buffer);
                status = json_get_int(json, "status");
                if(status != SUCCESS){
                    printf("Failed to get problem\n");
                    cJSON_Delete(json);
                    break;
                }
                char *problemTitle = json_get_string(json, "title");
                int problemDifficulty = json_get_int(json, "difficulty");

                printf("Title: %s, Difficulty: %d\n", problemTitle, problemDifficulty);

                char probStatementPath[200];

                sprintf(probStatementPath, "./tempProblemStatement%d-%s.txt", problemID, permanentUsername);

                if(receiveFile(fd, probStatementPath) < 0){
                    break;
                }

                printf("Problem Statement:\n");
                readFile(probStatementPath);
                
                deleteFile(probStatementPath);
                cJSON_Delete(json);
                break;
            case SUBMIT_SOLUTION:
                printf("Enter problem ID: ");
                scanf("%d", &problemID);
                printf("Give absolute path to your solution file(should be C code): ");
                scanf("%s", problemStatementPath);
                if((fileSize1 = getFileSize(problemStatementPath)) < 0){
                    break;
                }

                cJSON *submitData = cJSON_CreateObject();
                cJSON_AddNumberToObject(submitData, "op", SUBMIT_SOLUTION);
                cJSON_AddNumberToObject(submitData, "id", problemID);

                json_str = cJSON_PrintUnformatted(submitData);
                write(fd, json_str, strlen(json_str));
                free(json_str);
                cJSON_Delete(submitData);

                read(fd, buffer, sizeof(buffer));
                cJSON *submitResponse = cJSON_Parse(buffer);
                status = json_get_int(submitResponse, "status");
                if(status != SUCCESS){
                    printf("Failed to submit solution\n");
                    cJSON_Delete(submitResponse);
                    break;
                }

                cJSON_Delete(submitResponse);
                if(sendFile(fd, problemStatementPath, fileSize1) < 0){
                    break;
                }

                read(fd, buffer, sizeof(buffer));
                submitResponse = cJSON_Parse(buffer);
                status = json_get_int(submitResponse, "status");
                message = json_get_string(submitResponse, "message");
                                
                printf("Submission status: %s\n", message);
                cJSON_Delete(submitResponse);

                break;
            case DUEL_USER:
                char opponentUsername[100];
                printf("Enter opponent's username: ");
                scanf("%s", opponentUsername);

                if(strcmp(opponentUsername, permanentUsername) == 0){
                    printf("You cannot duel yourself!\n");
                    break;
                }

                cJSON *duelData = cJSON_CreateObject();
                cJSON_AddNumberToObject(duelData, "op", DUEL_USER);
                cJSON_AddStringToObject(duelData, "opponent", opponentUsername);

                json_str = cJSON_PrintUnformatted(duelData);
                write(fd, json_str, strlen(json_str));
                free(json_str);
                cJSON_Delete(duelData);

                read(fd, buffer, sizeof(buffer));

                cJSON *duelResponse = cJSON_Parse(buffer);
                status = json_get_int(duelResponse, "status");
                cJSON_Delete(duelResponse);

                if(status == FAILURE){
                    printf("Failed to start duel\n");
                    break;
                }else if(status == GO_TO_DUEL_WAITING_MENU){
                    printf("Duel request sent. Waiting for opponent to accept...\n");
                    printf("Either press 2 to exit or wait for the duel to start automatically when the opponent accepts the request.\n");

                    fd_set readfds;
                    while(1){
                        FD_ZERO(&readfds);
                        FD_SET(STDIN_FILENO, &readfds);
                        FD_SET(fd, &readfds);

                        int max_fd = (fd > STDIN_FILENO) ? fd : STDIN_FILENO;
                        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);

                        if(activity < 0){
                            perror("Select error");
                            break;
                        }

                        if(FD_ISSET(STDIN_FILENO, &readfds)){
                            fgets(buffer, sizeof(buffer), stdin);
                            
                            if(buffer[0] == '2'){
                                cJSON *cancelData = cJSON_CreateObject();
                                cJSON_AddNumberToObject(cancelData, "op", WITHDRAW_CHALLENGE);
                                json_str = cJSON_PrintUnformatted(cancelData);
                                write(fd, json_str, strlen(json_str));
                                free(json_str);
                                cJSON_Delete(cancelData);
                                printf("Duel request withdrawn. Returning to user menu.\n");
                                break;
                            }
                        }

                        if(FD_ISSET(fd, &readfds)){
                            read(fd, buffer, sizeof(buffer) - 1);
                            cJSON *response = cJSON_Parse(buffer);
                            int status = json_get_int(response, "status");
                            cJSON_Delete(response);
                            if(status == GO_TO_DUEL_MENU){
                                printf("Duel request accepted! Starting duel...\n");
                                currentMenu = DUEL_MENU;

                                cJSON *ack = cJSON_CreateObject();
                                cJSON_AddNumberToObject(ack, "op", DUEL_ACCEPTED);
                                char* ackStr = cJSON_PrintUnformatted(ack);
                                write(fd, ackStr, strlen(ackStr));
                                free(ackStr);
                                cJSON_Delete(ack);

                                break;
                            }
                        }
                    }
                }else{
                    currentMenu = DUEL_MENU;
                }

                break;
            case VIEW_FULL_LEADERBOARD:
            {
                int pageNumber;
                printf("Enter page number: ");
                scanf("%d", &pageNumber);
                cJSON *leaderboardRequest = cJSON_CreateObject();
                cJSON_AddNumberToObject(leaderboardRequest, "op", VIEW_FULL_LEADERBOARD);
                cJSON_AddNumberToObject(leaderboardRequest, "page", pageNumber);

                char *json_str = cJSON_PrintUnformatted(leaderboardRequest);
                write(fd, json_str, strlen(json_str));
                free(json_str);
                cJSON_Delete(leaderboardRequest);
                
                bytesRead = read(fd, buffer, sizeof(buffer));
                if(bytesRead <= 0){
                    printf("No response from server\n");
                    break;
                }

                cJSON *leaderboardResponse = cJSON_Parse(buffer);
                int status = json_get_int(leaderboardResponse, "status");
                if(status != SUCCESS){
                    printf("Failed to retrieve leaderboard\n");
                    cJSON_Delete(leaderboardResponse);
                    break;
                }
                int count = json_get_int(leaderboardResponse, "count");
                cJSON *entriesArray = cJSON_GetObjectItem(leaderboardResponse, "entries");
                printf("Leaderboard - Page %d:\n", pageNumber);
                if(entriesArray != NULL){
                    for(int i = 0; i < count; i++){
                        cJSON *entryJSON = cJSON_GetArrayItem(entriesArray, i);
                        if(entryJSON != NULL){
                            char *username = json_get_string(entryJSON, "username");
                            int elo = json_get_int(entryJSON, "elo");   
                            printf("Username: %s, ELO: %d\n", username, elo);
                        }
                    }
                }
                cJSON_Delete(leaderboardResponse);

                break;
            }
            case VIEW_SPECIFIC_ELO:
                char queryUsername[100];
                printf("Enter username to query ELO: ");
                scanf("%s", queryUsername);

                cJSON *eloRequest = cJSON_CreateObject();
                cJSON_AddNumberToObject(eloRequest, "op", VIEW_SPECIFIC_ELO);
                cJSON_AddStringToObject(eloRequest, "username", queryUsername);

                char *eloRequestStr = cJSON_PrintUnformatted(eloRequest);
                write(fd, eloRequestStr, strlen(eloRequestStr));
                free(eloRequestStr);
                cJSON_Delete(eloRequest);

                read(fd, buffer, sizeof(buffer));
                cJSON *eloResponse = cJSON_Parse(buffer);
                int eloStatus = json_get_int(eloResponse, "status");
                if(eloStatus != SUCCESS){
                    printf("Failed to retrieve ELO\n");
                    cJSON_Delete(eloResponse);
                    break;
                }
                int elo = json_get_int(eloResponse, "elo");
                printf("User %s has ELO: %d\n", queryUsername, elo);
                cJSON_Delete(eloResponse);
                break;
            case GO_TO_MODIFY_PROBLEMS_MENU:
                currentMenu = MODIFY_PROBLEMS_MENU;
                break;
            case GO_TO_LOGIN_MENU:
                currentMenu = LOGIN_MENU;
                break;
            case GO_TO_ADMIN_MENU:
                currentMenu = ADMIN_MENU;
                break;
            case GO_TO_USER_MENU:
                currentMenu = USER_MENU;
                break;
        }
        memset(buffer, 0, sizeof(buffer));
    }

    close(fd);
    return 0;
}