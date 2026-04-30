#!/bin/bash

LOGFILE="concurrency_results.log"
# Clear the log file if it exists
> $LOGFILE

echo "Launching 3 concurrent login attempts..."
echo "All output is being sent to $LOGFILE"

# Use stdbuf to prevent jumbling and append to the same file
stdbuf -oL ./login.exp "user1" "user1" >> $LOGFILE 2>&1 &
stdbuf -oL ./login.exp "user2" "user2" >> $LOGFILE 2>&1 &
stdbuf -oL ./login.exp "user3" "user3" >> $LOGFILE 2>&1 &

wait
