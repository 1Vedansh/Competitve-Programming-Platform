#!/bin/bash

LOGFILE="register.log"
# Clear the log file
> $LOGFILE

echo "Launching 20 CONCURRENT registration attempts..."
echo "Monitoring logs in $LOGFILE..."

# Loop 20 times to fire off the background processes
for i in {1..5}
do
    # Define username/password based on index
    USER="user$i"
    PASS="user$i"
    
    # Launch register.exp in the background (&)
    # stdbuf -oL ensures output isn't held in memory and jumbled
    stdbuf -oL ./register.exp "$USER" "$PASS" >> $LOGFILE 2>&1 &
done

# Wait for all 20 background processes to finish
wait

echo "Blitz complete. Results recorded in $LOGFILE."
# Count successful signups in the log
SUCCESS_COUNT=$(grep -c "Signup successful" $LOGFILE)
echo "Total successful registrations: $SUCCESS_COUNT/20"