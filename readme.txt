Compile with make file and run
1. Navigate to the tic-tac-toe folder
2. Open Terminal
3. Type: make
4. Type: chmod +x run_game.sh (Do once only)
5. Type: ./run_game.sh 

Example Commands
make : Compile all source file and links libraries
make clean : Removes server, client, and game.log file
./server : Start the game for server
./client : Connects to localhost

Rules
-3 players are needed to start. 
-Player enters their name upon connection.
-Each player choose a symbol (X,Y,Z)
-A 4x4 Grid will be displayed and players can type "1-12"
Win Condition
-The first player to have 3 symbol in a row win.
-Once game finish, the game will restart in 5 seconds.

Modes Supported
-localhost
-persistent
-hybrid-concurrency mode 