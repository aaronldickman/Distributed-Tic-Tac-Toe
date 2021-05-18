#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
/*
 * @ttts.c
 * version 4
 * TicTacToe game
 * By Jeremy Gage, extended by Aaron Dickman
 * CSCI 3800
 * Lab 6
 * Instructor: Dave Ogle
 * Created 23Feb2021
 */

const unsigned char VERSION = 0x06;

const unsigned char NEWGAME = 0x00;
const unsigned char MOVE = 0x01;
const unsigned char GAMEOVER = 0x02;
const unsigned char RESUME = 0x03;

const int MAX_GAMES = 5;
const int MAX_RESENDS = 3;
const int GAME_TIMEOUT = 30;

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

#define MC_PORT 1818
#define MC_GROUP "239.0.0.1"
#define ROWS  3
#define COLUMNS  3
#define TIMETOWAIT 5

struct message{
    unsigned char version;
    unsigned char command;
    unsigned char position;
    unsigned char id;
    unsigned char seqNum;
};

struct game{
    char id;
    double timeSinceCommunciation;
    int isInProgress;
    char board[ROWS][COLUMNS];
    int currentSeqNum;
    int resends;
    struct message lastMessage;
    int socket;
    unsigned char buffer[sizeof(struct message)];
    int bytesOfCurrentMessage;
};
/**
 * Creates a UDP socket configured to be a member of a multicast group as defined in spec document.
 * In practice if a server goes down in the middle of a game, a client can multicast to this group to request a server to pick up the game.
 * @param sd uninitialized int to be converted into socket
 * @return 1 on success, 0 on failure
 */
int createMulticastSocket(int *sd, struct sockaddr_in *multicast_address);
/**
 * Creates a TCP socket configured to listen for incoming connection requests from potential clients.
 * @param sd uninitialized int to be converted into socket
 * @param portNum
 * @param server_address
 * @return 1 on success, 0 on failure
 */
int createListeningSocket(int *sd, int portNum, struct sockaddr_in *server_address);
/**
 * Brute force checks if a gameover state on a board has been reached.
 * @param board
 * @return
 */
int checkwin(char board[ROWS][COLUMNS]);
/**
 * does the necessary math to transform move into a row and column, determines the mark to place on the board, and modifies board accordingly
 * assumes you have already validated the moves legality using validateMove()
 * @param game
 * @param move 0x01 - 0x09, corresponds to a square on the board.
 */
void makeMoveOnBoard(struct game *game, unsigned char move, int player);
/**
 * Control logic for a singular round of tic tac toe.
 * Parses a client's move, validates + makes the move on board, then replies with a move if the game isn't over.
 * If the game is over after the client's move, will instead reply with a GAMEOVER message (see protocol)
 * @param game
 * @param clientMove
 * @param sd
 */
void tictactoeRound(struct game *game, unsigned char clientMove);
/**
 * Terrible AI algorithm. Returns a random valid move.
 * Will return 255 if 50 unsuccessful attempts to find a valid move are made.
 * @param board
 * @return 1-9 move for the server to make.
 */
unsigned char getAIMove(char board[ROWS][COLUMNS]);
/**
 * Finds the first available game ID within the games array and returns the ID. Returns -1 if no games are available
 * @param games
 * @return id of game if a game is available, otherwise -1
 */
int getAvailableGameID(const struct game games[MAX_GAMES]);
/**
 * Initializes a game object to have all necessary information to begin a game.
 * @param game to be initialized
 * @return 0
 */
int initializeGame(struct game *game);
/**
 * Increments the value of .timeSinceCommunication for all game objects within games where .isInProgress is true.
 * @param games
 * @param time number of seconds to increment .timeSinceCommunication by
 * @param resetID if this is not -1, the game with this ID will have its time set to 0 instead of incremented. otherwise it is ignored.
 */
void incrementTimeSinceCommunication(struct game games[], double time, int resetID);
/**
 * For any game whose timeSinceCommunication > GAME_TIMEOUT:
 * If resends equals or exceeds MAX_RESENDS, end the game (set isInProgress & timeSinceCommunication to 0)
 * If resends is less than MAX_RESENDS, resend the last message to the client.
 * For any game within the array whose timeSinceCommunication > GAME_TIMEOUT _AND_ whose resends is greater than MAX_RESENDS .isInProgress = 0 and .timeSinceCommunication = 0
 */
void manageTimedOutGames(struct game *games);
/**
 * Validates a move against a tic tac toe board to make sure the square is available
 * (ripped from lab 4 client code)
 * @param move
 * @param board
 * @return 1 on valid, 0 on invalid
 */
int validateMove(unsigned char move, char board[ROWS][COLUMNS]);
/**
 * Given an unparsed buffer containing data from a client, assigns values to a message object appropriately according to protocol
 * @param buffer
 * @return Initialized message object
 */
struct message parsePacketFromBuffer(unsigned char buffer[]);
/**
 * Wrapper function for sendto which includes log commands.
 * @param game
 * @param message
 */
void sendPacketToClient(struct game* game, struct message* message);
/**
 * Copies a 1 dimensional array of each square's state to a 3x3 board state.
 * @param game
 * @param buffer
 */
void copyBoardStateToGame(struct game* game, unsigned char buffer[9]);
/**
 * Generates a reply (move or gameover) based on board state.
 * Checks if the board is in a game over state, if so returns a valid GAMEOVER packet
 * Otherwise returns a MOVE packet with a valid move.
 * @param game
 * @return the generated reply
 */
struct message getServerReply(struct game* game);
int main (int argc, char *argv[]) {
    srand(time(NULL));

    struct timespec start, stop;
    double timeDiffSeconds;
    struct sockaddr_in server_address, multicast_address;
    struct sockaddr_in from_address;
    unsigned short portNum;
    socklen_t fromLength;
    int rc;
    unsigned char bufferIn[sizeof(struct message)];
    fd_set socketSet;
    int maxSD;
    if (argc != 2) {
        printf("usage is: ttts <port-number>\n");
        exit(EXIT_FAILURE);
    }

    int multicastSD;
    if(!createMulticastSocket(&multicastSD, &multicast_address)){
        printf("\nCouldn't create multicast socket, exiting.");
        exit(EXIT_FAILURE);
    }

    portNum = strtol(argv[1], NULL, 10);
    printf("host: %hu, nbo: %hu", portNum, htons(portNum));
    int listeningSD;
    if(!createListeningSocket(&listeningSD, portNum, &server_address)){
        printf("\nCouldn't create listening socket, exiting.");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for play requests...\n");

    struct game games[MAX_GAMES];
    for(int n=0; n < MAX_GAMES; n++){
        games[n].id = n;
        games[n].timeSinceCommunciation = 0;
        games[n].isInProgress = 0;
        memset(games[n].buffer, 0, sizeof(struct message));
        games[n].bytesOfCurrentMessage = 0;
        games[n].socket = 0;
    }

    while (1) {
        // manage socketSet for select
        FD_ZERO(&socketSet);
        FD_SET(multicastSD, &socketSet);
        FD_SET(listeningSD, &socketSet);
        maxSD = multicastSD > listeningSD ? multicastSD : listeningSD;

        for(int n=0; n < MAX_GAMES; n++){
            if(games[n].socket > 0){
                FD_SET(games[n].socket, &socketSet);
                if(games[n].socket > maxSD)
                    maxSD= games[n].socket;
            }
        }

        memset(bufferIn, 255, sizeof(struct message));
        fromLength=sizeof(struct sockaddr_in);
        clock_gettime(CLOCK_REALTIME, &start);
        struct timeval tv;
        tv.tv_sec = TIMETOWAIT;
        tv.tv_usec = 0;
        select(maxSD+1, &socketSet, NULL, NULL, &tv);
        clock_gettime(CLOCK_REALTIME, &stop);
        timeDiffSeconds = (stop.tv_sec - start.tv_sec) + (double)(stop.tv_nsec - start.tv_nsec) / (double)1000000000L;

        if(FD_ISSET(multicastSD, &socketSet)){
            rc = recvfrom(multicastSD, bufferIn, sizeof(struct message), 0, (struct sockaddr *)&from_address, &fromLength);
            if(rc == 2){ //ignore any malformed multicasts
                int id = getAvailableGameID(games);
                if(bufferIn[0] == VERSION && id != -1){
                    const int replySize = 3;
                    unsigned char reply[replySize];
                    reply[0] = VERSION;
                    unsigned short nboPort = htons(portNum);
                    memcpy(reply+1, &nboPort, 2);
                    printf("[DATA]:\tSENT\t%x %x %x\n", reply[0], reply[1], reply[2]);
                    sendto(multicastSD, reply, replySize, 0, (struct sockaddr *)&from_address, fromLength);
                }
            }
        }
        if(FD_ISSET(listeningSD, &socketSet)){
            printf("[ACTION]:\tGot connection request from a client, searching for an open game id...\n");
            int id = getAvailableGameID(games);
            if(id == -1){
                //When listen() is called the OS implicitly begins accepting connections even before accept() is called
                //So if we can't find an available game ID we will just accept() and then drop the connection
                //max connections = MAX_GAMES so this hopefully shouldn't ever happen
                printf("[ACTION]:\tCouldn't find available game ID for game, rejecting.\n");
                int rejectedSD = accept(listeningSD, (struct sockaddr *)&from_address, &fromLength);
                close(rejectedSD);
            }
            else{
                printf("[ACTION]:\tCreated socket for game id %i\n", id);
                games[id].socket = accept(listeningSD, (struct sockaddr*)&from_address, &fromLength);
            }
        }
        //retrieving data from connected clients
        incrementTimeSinceCommunication(games, timeDiffSeconds, -1);

        for(int id=0; id < MAX_GAMES; id++){
            if(FD_ISSET(games[id].socket, &socketSet)){
                memset(bufferIn, 0, sizeof(struct message));
                printf("[ACTION]:\tData available for game id %i\n", id);
                rc = read(games[id].socket, &bufferIn, sizeof(struct message));
                games[id].timeSinceCommunciation = 0;

                if(rc <= 0){ //disconnect
                    printf("[ACTION]:\tBroken pipe for game %i, ending game and cleaning up\n", id);
                    games[id].isInProgress = 0;
                    close(games[id].socket);
                    games[id].socket = 0;
                    continue;
                }
                else if (games[id].bytesOfCurrentMessage + rc > sizeof(struct message)){
                    printf("[ACTION]:\tReceived (%i (rc) + %i (current)) bytes of data from client for current message, max is %lu. Attempting to create a complete message\n",
                           rc, games[id].bytesOfCurrentMessage, sizeof(struct message));
                    int bytesLeft = sizeof(struct message) - games[id].bytesOfCurrentMessage;
                    memcpy(games[id].buffer + games[id].bytesOfCurrentMessage, bufferIn, bytesLeft);
                    games[id].bytesOfCurrentMessage += bytesLeft;
                    continue;
                }
                else{
                    memcpy(games[id].buffer + games[id].bytesOfCurrentMessage, bufferIn, rc);
                    games[id].bytesOfCurrentMessage += rc;
                    printf("[ACTION]:\tReceived %i bytes for game %i, at %i / %lu bytes\n", rc, id, games[id].bytesOfCurrentMessage, sizeof(struct message));
                }

                //after processing input if we have a complete message, handle it
                if(games[id].bytesOfCurrentMessage == sizeof(struct message)){
                    struct message messageIn = parsePacketFromBuffer(games[id].buffer);
                    games[id].bytesOfCurrentMessage = 0;
                    memset(games[id].buffer, 0, sizeof(struct message));

                    if(messageIn.version != VERSION){
                        // client is using wrong protocol, close up
                        printf("[ACTION]:\tReceived bad version number (%i) from client, disconnecting...\n", messageIn.version);
                        games[id].isInProgress = 0;
                        games[id].bytesOfCurrentMessage = 0;
                        close(games[id].socket);
                        games[id].socket = 0;
                        continue;
                    }
                    if(messageIn.command == NEWGAME) {
                        if (games[id].isInProgress) { //handle protocol v4/5 issue related to dropped seq#1 packet
                            printf("[ACTION]:\tReceived NEWGAME request from client with game already in progress.\n");
                            printf("[ACTION]:\tAssuming client didn't get the first move resending last message.\n");
                            sendPacketToClient(&games[id], &games[id].lastMessage);
                        }
                        else {
                            initializeGame(&games[id]);
                            games[id].currentSeqNum = 1;
                            struct message reply = getServerReply(&games[id]);
                            memcpy(&games[id].lastMessage, &reply, sizeof(struct message));
                            printf("[ACTION]\tCreated NEWGAME with id %i\n", id);
                            printf("[ACTION]\tSent MOVE ( %i ) for game %i\n", reply.position, id);
                            sendPacketToClient(&games[id], &reply);
                        }
                    }
                    else if(messageIn.command == MOVE){
                        if(!games[id].isInProgress){
                            printf("[ACTION]:\tReceived a move for a game not in progress, ignoring.\n");
                        }
                        else if(messageIn.id != id){
                            printf("[ACTION]:\tReceived move from client for game %i, but client is associated with game %i, ignoring\n",
                                   messageIn.id, id);
                        }
                        else{
                            //correct seq#, advance the game state
                            if (messageIn.seqNum == games[messageIn.id].currentSeqNum + 1) {
                                games[id].resends = 0;
                                tictactoeRound(&games[id], messageIn.position);
                            }
                                //dupe seq#, resend previous message
                            else if (messageIn.seqNum == games[id].currentSeqNum - 1) {
                                games[messageIn.id].resends++;
                                printf("[ACTION]\tGame %i sent dupe message (sent seq %i, should be %i), resending last message (%i / %i)\n",
                                       id, messageIn.seqNum, games[id].currentSeqNum + 1,
                                       games[messageIn.id].resends, MAX_RESENDS);
                                sendPacketToClient(&games[id], &games[id].lastMessage);
                            }
                                //client is more than 1 move out of sync, abandon all hope
                            else if (messageIn.seqNum > games[messageIn.id].currentSeqNum + 1) {
                                printf("[ACTION]\tGame %i sent message more than 2 out of sync (sent %i, should be %i), ending game.\n",
                                       messageIn.id, messageIn.seqNum, games[messageIn.id].currentSeqNum + 1);
                                games[messageIn.id].isInProgress = 0;
                            }
                        }
                    }
                    else if(messageIn.command == GAMEOVER){
                        if(messageIn.id != id){
                            printf("[ACTION]:\tReceived game over command for game %i but client is associated with game %i, ignoring.\n",
                                   messageIn.id, id);
                        }
                        else if(checkwin(games[id].board) == -1){
                            printf("[ACTION]:\tReceived game over command for game %i but board is not in an endgame state, ignoring\n", id);
                        }
                        else{
                            printf("[ACTION]:\tReceived game over command for game %i, cleaning up game + socket info.\n", id);
                            close(games[id].socket);
                            games[id].socket = 0;
                            games[id].isInProgress = 0;
                        }
                    }
                    else if(messageIn.command == RESUME){
                        initializeGame(&games[id]);
                        games[id].currentSeqNum = bufferIn[4];
                        unsigned char gameState[ROWS*COLUMNS];
                        rc = read(games[id].socket, gameState, ROWS*COLUMNS);

                        if(rc != ROWS*COLUMNS){
                            printf("[ACTION]:\tReceived RESUME command for game %i, but not full board state. Dropping game.\n", id);
                            close(games[id].socket);
                            games[id].socket = 0;
                            games[id].isInProgress = 0;
                        }
                        else{
                            printf("[ACTION]:\tReceived RESUME command.\n");
                            copyBoardStateToGame(&games[id], gameState);
                            games[id].currentSeqNum++;
                            struct message reply = getServerReply(&games[id]);
                            memcpy(&games[id].lastMessage, &reply, sizeof(struct message));
                            sendPacketToClient(&games[id], &reply);
                        }

                    }
                    else{
                        printf("[ACTION]:\tReceived invalid command from game with id %i, ignoring\n", id);
                    }
                }
            }
        }
        manageTimedOutGames(games);
    }
}

int createMulticastSocket(int *sd, struct sockaddr_in *multicast_address) {
    struct ip_mreq mreq;

    multicast_address->sin_family = AF_INET;
    multicast_address->sin_addr.s_addr = htonl(INADDR_ANY);
    multicast_address->sin_port = htons(MC_PORT);

    mreq.imr_multiaddr.s_addr = inet_addr(MC_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    *sd = socket(AF_INET, SOCK_DGRAM, 0);
    if(*sd==-1){
        perror("createMulticastSocket:\tsocket():");
        return 0;
    }
    socklen_t addrLen = sizeof(struct sockaddr_in);
    int rc = bind(*sd, (struct sockaddr *)multicast_address, addrLen);
    if(rc != 0){
        perror("createMulticastSocket:\tbind():");
        return 0;
    }
    rc = setsockopt(*sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    if(rc != 0){
        perror("createMulticastSocket:\tsetsockopt():");
        return 0;
    }

    return 1;
}

int createListeningSocket(int *sd, int portNum, struct sockaddr_in *server_address){
    struct timeval tv;
    tv.tv_sec = TIMETOWAIT;
    tv.tv_usec = 0;

    server_address->sin_family = AF_INET;
    server_address->sin_port = htons(portNum);
    server_address->sin_addr.s_addr = INADDR_ANY;
    *sd = socket(AF_INET, SOCK_STREAM, 0);  //initialize datagram socket
    if (*sd==-1){
        perror("createListeningSocket:\tsocket():");
        return 0;
    }
    if (setsockopt(*sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) {//set socket timeout
        perror("createListeningSocket:\tsetsockopt():");
        close(*sd);
        return 0;
    }
    socklen_t fromLength = sizeof(struct sockaddr_in);
    int rc = bind(*sd, (struct sockaddr *)server_address, fromLength);
    if(rc != 0){
        perror("createListeningSocket:\tbind():");
        return 0;
    }
    rc = listen(*sd, MAX_GAMES);
    if(rc != 0){
        perror("createListeningSocket:\tlisten():");
        return 0;
    }
    return 1;
}

int checkwin(char board[ROWS][COLUMNS])
{
    /************************************************************************/
    /* brute force check to see if someone won, or if there is a draw       */
    /* return a 0 if the game is 'over' and return -1 if game should go on  */
    /************************************************************************/
    if (board[0][0] == board[0][1] && board[0][1] == board[0][2] ) // row matches
        return 1;

    else if (board[1][0] == board[1][1] && board[1][1] == board[1][2] ) // row matches
        return 1;

    else if (board[2][0] == board[2][1] && board[2][1] == board[2][2] ) // row matches
        return 1;

    else if (board[0][0] == board[1][0] && board[1][0] == board[2][0] ) // column
        return 1;

    else if (board[0][1] == board[1][1] && board[1][1] == board[2][1] ) // column
        return 1;

    else if (board[0][2] == board[1][2] && board[1][2] == board[2][2] ) // column
        return 1;

    else if (board[0][0] == board[1][1] && board[1][1] == board[2][2] ) // diagonal
        return 1;

    else if (board[2][0] == board[1][1] && board[1][1] == board[0][2] ) // diagonal
        return 1;

    else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
             board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' &&
             board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')

        return 0; // Return of 0 means game over
    else
        return  - 1; // return of -1 means keep playing
}

void tictactoeRound(struct game *game, unsigned char clientMove){
    const int CLIENT = 2;
    game->currentSeqNum += 2; //account for both client and server response
    if(validateMove(clientMove, game->board)){
        makeMoveOnBoard(game, clientMove, CLIENT);
        printf("[ACTION]:\tFor game %i, Player 2 made MOVE: %i\n", game->id, clientMove);
    }
    else{
        printf("[ACTION]:\tFor game %i, Player 2 made illegal MOVE: %i, aborting game\n", game->id, clientMove);
        game->isInProgress = 0;
        return;
    }
    struct message reply = getServerReply(game);
    sendPacketToClient(game, &reply);
    memcpy(&game->lastMessage, &reply, sizeof(struct message));
}

void makeMoveOnBoard(struct game *game, unsigned char move, int player){
    char mark = (player%2==1) ? 'X' : 'O';
    int row = (int)((move-1) / ROWS);
    int column = (move-1) % COLUMNS;
    game->board[row][column] = mark;
}

unsigned char getAIMove(char board[ROWS][COLUMNS]){
    unsigned char move;
    int n = 0;
    do{
        move = (rand() % 9) + 1;
        n++;
    }while(!validateMove(move, board) && n < 50);
    if(n == 50) //probably passed a bad board.
        return 255;
    else
        return move;
}

int getAvailableGameID(const struct game games[MAX_GAMES]){
    for(int id = 0; id < MAX_GAMES; id++){
        if(!games[id].isInProgress)
            return id;
    }
    return -1;
}

int initializeGame(struct game *game){
    game->isInProgress = 1;
    game->bytesOfCurrentMessage = 0;
    game->timeSinceCommunciation = 0;
    game->currentSeqNum = 0;
    game->resends = 0;
    memset(&game->lastMessage, 0, sizeof(struct message));
    /* this just initializing the shared state aka the board */
    int count = 1;
    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++){
            game->board[i][j] = count + '0';
            count++;
        }
    return 0;
}

//ripped from client from lab4
///Very simple function to validate user input
int validateMove(unsigned char move, char board[ROWS][COLUMNS]){
    //move corresponds to a square on the board
    if(move < 0x01 || move > 0x09)
        return 0;
    else{
        //simple math to convert our square number to array indices
        int row = (int)((move-1) / ROWS);
        int column = (move-1) % COLUMNS;
        //squares are initialized to their corresponding number, and are set to X/O after a move
        //if square == number, then we know its still available
        if(board[row][column] == (move+'0')){
            return 1;
        }
        else
            return 0;
    }
}

void incrementTimeSinceCommunication(struct game games[], const double time, const int resetID){
    for(int n = 0; n < MAX_GAMES; n++){
        if(n == resetID){
            games[n].timeSinceCommunciation = 0;
        }
        else if(games[n].isInProgress){
            games[n].timeSinceCommunciation += time;
        }
    }
}

void manageTimedOutGames(struct game *games){
    for(int n = 0; n < MAX_GAMES; n++){
        if(games[n].isInProgress && games[n].timeSinceCommunciation > GAME_TIMEOUT){
            if(games[n].resends < MAX_RESENDS){
                games[n].resends++;
                games[n].timeSinceCommunciation = 0;
                printf("[ACTION]:\tFor game %i, resent last message ( %i / %i resends )\n", games[n].id, games[n].resends, MAX_RESENDS);
                sendPacketToClient(&games[n], &games[n].lastMessage);
            }
            else{
                printf("[ACTION]:\tPruned timed out game with ID %i\n", games[n].id);
                games[n].isInProgress = 0;
                games[n].timeSinceCommunciation = 0;
            }
        }
    }
}

struct message parsePacketFromBuffer(unsigned char *buffer) {
    struct message packet;
    packet.version = buffer[0];
    packet.command = buffer[1];
    packet.position = buffer[2];
    packet.id = buffer[3];
    packet.seqNum = buffer[4];
    printf("[DATA]\t\tRECVD\t%i\t%i\t%i\t%i\t%i\n",
           packet.version, packet.command, packet.position, packet.id, packet.seqNum);
    return packet;
}

void sendPacketToClient(struct game *game, struct message *message) {
    printf("[DATA]\t\tSENT\t%i\t%i\t%i\t%i\t%i\n",
           message->version, message->command, message->position, message->id, message->seqNum);
    send(game->socket, message, sizeof(struct message), 0);
}

void copyBoardStateToGame(struct game *game, unsigned char *buffer) {
    for(int n=0; n < 9; n++){
        int row = (int)((n) / ROWS);
        int column = (n) % COLUMNS;
        game->board[row][column] = buffer[n];
    }
}

struct message getServerReply(struct game *game) {
    const int SERVER = 1;
    struct message reply;
    reply.version = VERSION;
    reply.id = game->id;
    reply.seqNum = game->currentSeqNum;

    int rc = checkwin(game->board);

    if(rc == -1){ //server needs to make a move in reply to client
        unsigned char move = getAIMove(game->board);
        if(move != 255){
            printf("[ACTION]:\tFor game %i, Player 1 made MOVE: %i\n", game->id, move);
            makeMoveOnBoard(game, move, SERVER);
            reply.command = MOVE;
            reply.position = move;
        }
        rc = checkwin(game->board);
        if(rc == 1 || rc == 0){
            printf("[ACTION]:\tFor game %i, game over state detected. Waiting for GAMEOVER message.\n", game->id);
        }
    }
    else{ // the game is over, send a GAMEOVER message to client to ack their final move
        printf("[ACTION]:\tFor game %i, game over state detected. Sending GAMEOVER message.\n", game->id);
        game->isInProgress = 0;
        reply.command = GAMEOVER;
        reply.position = 0;
    }
    return reply;
}


#pragma clang diagnostic pop