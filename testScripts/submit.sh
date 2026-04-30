#!/bin/bash

LOGFILE="submit.log"
> $LOGFILE

echo "Launching 20 CONCURRENT submissions..."
echo "Monitoring $LOGFILE..."

for i in {1..5}
do
    USER="user$i"
    PASS="user$i"
    
    # Run the submission script in the background
    stdbuf -oL ./submit.exp "$USER" "$PASS" >> $LOGFILE 2>&1 &

    sleep 0.2
done

wait

echo "Stress test complete."