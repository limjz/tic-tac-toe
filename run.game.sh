#What is this file?
#This is a bash script to compile and run the server and multiple clients
#To run:
#1st: Open Terminal
#2nd: Navigate to the Tic-Tac_Toe
#3rd: Type: chmod +x run_game.sh (Do once only)
#4th: Type: ./run_game.sh

#!/bin/bash

# 1. Compile the code first to ensure everything is fresh
echo "Compiling..."
make

# Check if compile was successful
if [ $? -ne 0 ]; then
    echo "Compilation failed! Fix errors before running."
    exit 1
fi

echo "Starting Server and Clients..."


xterm -T "SERVER" -e ./server &
sleep 1
xterm -T "PLAYER 1" -e ./client &
xterm -T "PLAYER 2" -e ./client &
xterm -T "PLAYER 3" -e ./client &

echo "All terminals launched!"