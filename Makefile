# Variables
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
INCLUDES = -I./Server -I./Utility -I./Client

# 1. Automatically find ALL .c files in their respective folders
SERVER_DIR_SRCS  = $(wildcard Server/*.c)
CLIENT_DIR_SRCS  = $(wildcard Client/*.c)
UTILITY_DIR_SRCS = $(wildcard Utility/*.c)

# 2. Define what goes into each executable
# Server needs everything in Server/ and Utility/
SERVER_SOURCES = $(SERVER_DIR_SRCS) $(UTILITY_DIR_SRCS)

# Client needs everything in Client/ and Utility/
CLIENT_SOURCES = $(CLIENT_DIR_SRCS) $(UTILITY_DIR_SRCS)

# 3. Convert .c lists to .o lists
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

# Default rule builds both
all: server client

# Link Server
server: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o server $(SERVER_OBJECTS) -lm

# Link Client
client: $(CLIENT_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o client $(CLIENT_OBJECTS) -lm

# Universal rule to compile any .c into .o
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean up
clean:
	rm -f server client
	find . -name "*.o" -delete