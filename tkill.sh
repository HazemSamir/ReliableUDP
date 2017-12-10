#!/bin/bash

ids[0]="$1"

index=0

while true
do
    ((index++))

    # get all child processes spawned by this/these ppid/s
    ids[$index]=$(ps -o pid --ppid ${ids[$index-1]} | \
      grep '[0-9]\{1,\}' | tr \\n ' ')

    # if no child processes found
    if [ ! "${ids[$index]}" ]
    then
        # quit
        break
    fi
done

# kill process from parent to all child processes
for i in $(seq 0 ${#ids[@]})
do
    if [ "${ids[$i]}" ]
    then
    	echo "kill ${ids[$i]}"
        kill ${ids[$i]} || true
    fi
done
