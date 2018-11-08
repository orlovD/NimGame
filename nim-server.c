#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* for read(), write() */
#include <sys/types.h> /* data types used in system calls */
#include <sys/socket.h> /* definitions of structures needed for sockets */
#include <netinet/in.h> /* constants and structures needed for Internet domain addresses */
#include <assert.h> /* asserts */
#include <errno.h> /* error messages */
#include <string.h> /* string functions */
#include "transport.h" /* common data with client */
#include <sys/select.h> /* select */
#include <fcntl.h> /* for manipulating file descriptor */

#define NUM_OF_HEAPS 4
#define DEFAULT_PORT 6325
#define MAX_NUM_OF_CLIENTS 9
#define MAX_ID 25
#define ALT(x, y) if(!(x)){(y);}

/* structure for client with buffered socket */
typedef struct Client {
	buffered_socket_t sock;
	client_status_t status;
} client_t;

client_t * clientList[MAX_ID]; /* array of maximum possible connected clients */
int p; /* maximal number of players in current game to connect */

/**
 * function checks for end of game
 * function sums cubes in heaps array
 * return 1 if there are no more cubes in array the game will end
 * return 0 otherwise
 **/
int checkGameEnd(short *heaps) {
	int i, sum = 0;
	for (i = 0; i < NUM_OF_HEAPS; i++) {
		sum += heaps[i];
	}
	return sum == 0;
}

/**
 * the function checks if user move that received from client is valid
 * returns 0 if the move is not valid, 1 otherwise
 **/
int isUserMoveValid(short heapIndex, short cubes_num, short * heaps) {
	return (heapIndex >= 0 && heapIndex <= NUM_OF_HEAPS && (cubes_num <= 1500) && (cubes_num > 0) && heaps[heapIndex] >= cubes_num);
}

/**
 * the function makes player move
 * takes the number of cubes from the chosen heap in the heap array
 **/
void playerMove(short *heaps, short heap, int num_of_cubes) {
	heaps[heap] -= num_of_cubes;
}

/**
 * the function counts current number of clients
 * returns current number of clients
 **/
char getClientsCount() {
	int id;
	char cnt = 0;
	for (id = 0; id < MAX_ID; id++) {
		client_t* client;
		client = clientList[id];
		if (client != NULL) {
			cnt++;
		}
	}
	return cnt;
}

/**
 * the function counts current number of players
 * returns current number of players
 **/
char getPlayersCount() {
	int id;
	char cnt = 0;
	for (id = 0; id < MAX_ID; id++) {
		client_t* client;
		client = clientList[id];
		if (client != NULL) {
			if (client->status != SPECTATOR) {
				cnt++;
			}
		}
	}
	return cnt;
}

/**
 * the function finds player that need to make move
 * returns player that need to make move
 **/
client_t * getCurrentPlayer() {
	int id;
	for (id = 0; id < MAX_ID; id++) {
		client_t* client;
		client = clientList[id];
		if (client != NULL) {
			if (client->status == YOUR_TURN) {
				return client;
			}
		}
	}
	return NULL;
}
char maxId = 0;
char getMaxId() {
	char current = maxId++;
	if (current >= MAX_ID) {
		return CLIENT_ID_INVALID;
	}
	return current;
}

/**
 * the function sends welcome message
 **/
void sendWelcomeMsg(buffered_socket_t * fd, int clientId, game_type_t gameType, char p, client_status_t clientStatus) {
	payload_t* pl = (payload_t*) malloc(sizeof(payload_t));
	pl->welcomeMsg.clientId = clientId;
	pl->welcomeMsg.gameType = gameType;
	pl->welcomeMsg.playersCnt = p;
	pl->welcomeMsg.clientStatus = clientStatus;
	game_msg_t* msg = createMessage(WELCOME, *pl);
	sendMessageB(fd, msg);
	destroyMsg(&msg);
	free(pl);
}

/**
 * the function sends reject message
 **/
void sendRejectMsg(int fd) {
	payload_t* pl = (payload_t*) malloc(sizeof(payload_t));
	pl->welcomeMsg.clientId = -1;
	pl->welcomeMsg.gameType = REJECTED;
	pl->welcomeMsg.playersCnt = -1;
	game_msg_t* msg = createMessage(WELCOME, *pl);
	sendMessage(fd, msg);
	destroyMsg(&msg);
	free(pl);
}

/**
 * the function sends turn response message
 **/
int sendTurnResponse(buffered_socket_t * fd, turn_resp_t l) {
	payload_t* pl = (payload_t*) malloc(sizeof(payload_t));
	pl->turnResp = l;
	game_msg_t* msg = createMessage(TURN_RESP, *pl);
	int res;
	res = sendMessageB(fd, msg);
	destroyMsg(&msg);
	free(pl);
	return res;
}

/**
 * the function sends end message
 **/
int sendEndMessage(buffered_socket_t * fd, end_game_t endGame, game_msg_t* statusMsg) {
	statusMsg->payload.status.endGame = endGame;
	int res;
	res = sendMessageB(fd, statusMsg);
	return res;
}

/**
 * the function sets next player as player that need to make move
 **/
void setNextPlayerAsCurrent() {
	client_t* currentPlayer = getCurrentPlayer();
	if (currentPlayer == NULL) {
		int id;
		for (id = 0; id < MAX_ID; id++) {
			client_t* client;
			client = clientList[id];
			if (client != NULL) {
				client->status = YOUR_TURN;
				break;
			}
		}
	} else {
		currentPlayer->status = PLAYING;
		int id;
		for (id = 0; id < MAX_ID; id++) {
			client_t* client;
			client = clientList[id];
			if (client != NULL) {
				if (currentPlayer->sock.socket == client->sock.socket) {
					int id2;
					for (id2 = id + 1; id2 <= MAX_ID + id; id2++) {
						client_t* client2;
						client2 = clientList[id2 % MAX_ID];
						if (client2 != NULL) {
							if (client2->status != SPECTATOR) {
								client2->status = YOUR_TURN;
								break;
							}
						}
					}
					break;
				}
			}
		}
	}
}

/**
 * the function determines client status
 **/
client_status_t determineNewClientStatus(int p) {
	char playersCount = getPlayersCount();
	if (playersCount < p) {
		return PLAYING;
	} else {
		return SPECTATOR;
	}
}

/**
 * the function updates client status
 **/
void updateClientsStatus(int * needToSendStatus) {
	char playersCount = getPlayersCount();
	int id;
	for (id = 0; id < MAX_ID && playersCount < p; id++) {
		client_t* client;
		client = clientList[id];
		if (client != NULL) {
			if (client->status == SPECTATOR) {
				client->status = PLAYING;
				*needToSendStatus = 1;
				playersCount++;
			}
		}

	}
}

/**
 * the function handles client disconnect
 **/
int onClientDisconnect(client_t * disconnected, int * isTurnDone, int * needToSendStatus) {
	int id;
	for (id = 0; id < MAX_ID; id++) {
		if (clientList[id] != NULL && clientList[id]->sock.socket == disconnected->sock.socket) {
			if (clientList[id]->status == YOUR_TURN) {
				*isTurnDone = 1;
			}
			free(disconnected);
			clientList[id] = NULL;
			break;
		}
	}
	//printf("onClientDisconnect getClientsCount=%d\n", getClientsCount());
	updateClientsStatus(needToSendStatus);
	return 1;
}

/**
 * the function handles received messages
 **/
void handleMsg(game_msg_t* msg, client_t * sourceClient, short * heap, int * isTurnDone, int * needToSendStatus) {
	char destination;
	switch (msg->type) {
	/* handle chat message */
	case CHAT:
		//fprintf(stderr,"CHAT: src=%d dst=%d\n",msg->payload.chat.srcId,msg->payload.chat.dstId);
		destination = msg->payload.chat.dstId;
		int id;
		for (id = 0; id < MAX_ID; id++) {
			client_t* destinationCl;
			destinationCl = clientList[id];
			if (destinationCl != NULL && (destination == -1 || destination - 1 == id)) {
				if (!sendMessageB(&destinationCl->sock, msg)) {
					onClientDisconnect(destinationCl, isTurnDone, needToSendStatus);
				}
			}
		}
		break;
	/* handle user move message */
	case TURN_REQ:
		//printf("turn_req\n");
		if (getCurrentPlayer()->sock.socket != sourceClient->sock.socket) {
			ALT(sendTurnResponse(&(sourceClient->sock), NOT_YOUR_TURN), onClientDisconnect(sourceClient, isTurnDone, needToSendStatus));
		} else {
			char heapIndex = msg->payload.turnReq.heapIndex;
			short cubes = msg->payload.turnReq.amount;
			int isLegal = isUserMoveValid(heapIndex, cubes, heap);
			if (isLegal) {
				playerMove(heap, heapIndex, cubes);
				//printf("move done\n");
			} else {
				//fprintf(stderr, "skipping turn - illegal move\n");
			}
			//printf("sending turn response\n");
			ALT(sendTurnResponse(&(sourceClient->sock), (isLegal) ? LEGAL : ILLEGAL), onClientDisconnect(sourceClient, isTurnDone, needToSendStatus));
			*isTurnDone = 1;
		}
		break;
	default:
		ALT(sendTurnResponse(&(sourceClient->sock), NOT_YOUR_TURN), onClientDisconnect(sourceClient, isTurnDone, needToSendStatus));
	}
}

/**
 * function sends reject message
 * and closes connection
 * returns 1 on socket error
 */
int rejectClient(int newConnection) {
	sendRejectMsg(newConnection); //send reject message to client
	if ((close(newConnection) == -1)) { //close connection
		printf("Error closing connection: %s!\n", strerror(errno));
		return 1; //exit on error
	}
	return 0;
}

/**
 * function sets non-blocking socket
 */
int setNonblocking(int fd) {
int flags;
/* if they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
/* otherwise, use the old way of doing it */
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

/* main function */
int main(int argc, char *argv[]) {
	int M; /* number of cubes in the heaps */
	game_type_t gameType = REGULAR; /* default game type */
	int port = DEFAULT_PORT; /* default port */
	struct sockaddr_in server_address, client_address; /* structure for socket parameters */
	int listSocket; /* listening socket descriptor */
	socklen_t clientLen; /* listening socket size */

	fd_set readSet; /* set of read-ready socket file descriptors for select */
	fd_set writeSet; /* set of write-ready socket file descriptors for select */
	/* check for arguments received in the command line */
	if (argc == 4 || argc == 5) { /* if there are 2 or 3 command line arguments */
		p = atoi(argv[1]);
		if (p < 2 || p > 9) { /* check if number of players is in range */
			printf("Error: Number of players should be between 2 and 9!\n");
			return 1; //exit on error
		}
		//printf("Can be simultaneously connected only %d clients. Max_num_of_players=%d\n", MAX_NUM_OF_CLIENTS, p);
		M = atoi(argv[2]);
		if (atoi(argv[3])) {
			gameType = MISERE;
		}
		if (argc == 5) { /* if there are 3 command line arguments */
			port = atoi(argv[4]);
		}
	} else {
		printf("Error: Wrong number of arguments received!\n");
		return 1; //exit on error
	}
	/* create listening socket */
	if ((listSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) { /* create socket of Internet domain, messages read in streams, OS chooses TCP */
		printf("Error creating socket: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	/* set server address parameters */
	memset((char *) &server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET; /* code for the address family, always set to the AF_INET */
	server_address.sin_port = htons(port); /* port number, a port number in host byte order converted to a port number in network byte order */
	server_address.sin_addr.s_addr = INADDR_ANY; /* field contains the IP address of the host, for server - IP address of the machine on which the server is running */
	/* reuse server address */
	int yes = 1;
	if ((setsockopt(listSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)) {
		printf("Error reusing port: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	/* bind the socket */
	if ((bind(listSocket, (struct sockaddr *) &server_address, sizeof(server_address))) == -1) {
		printf("Error binding socket: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	/* listen on the created socket, queue of MAX_NUM_OF_CLIENTS length */
	if (listen(listSocket, MAX_NUM_OF_CLIENTS) == -1) {
		printf("Error listening to socket: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	/* initialize heaps array */
	short heaps[NUM_OF_HEAPS] = { M, M, M, M };
	/* main loop of the game */
	while (1) {
		FD_ZERO(&readSet); /* initialize set of read-ready sockets */
		FD_ZERO(&writeSet); /* initialize set of write-ready sockets */

		int highSD = listSocket; /* highest socket descriptor */
		FD_SET(listSocket, &readSet); /* add listening socket to read-ready set */
		int numInList;
		/* add clients to read and write ready sets */
		for (numInList = 0; numInList < MAX_ID; numInList++) {
			if (clientList[numInList] != NULL) {
				buffered_socket_t sock = clientList[numInList]->sock;
				int fd = sock.socket;
				FD_SET(fd, &readSet);
				if (sock.rxBuffPos > 0) {
					FD_SET(fd, &writeSet);
				}
				if (fd > highSD) {
					highSD = fd;
				}
			}
		}
		/* select active socket */
		select(highSD + 1, &readSet, &writeSet, (fd_set *) 0, NULL);
		int isTurnDone = 0;
		int needToSendStatus = 0;
		/* try to send messages to active write ready socket */
		int id;
		for (id = 0; id < MAX_ID; id++) {
			client_t* client;
			client = clientList[id];
			if (client != NULL) {
				if (FD_ISSET(client->sock.socket, &writeSet)) {
					ALT(sendMessageB(&client->sock,NULL), onClientDisconnect(client, &isTurnDone, &needToSendStatus));
				}
			}
		}
		/* listening socket is read-ready - new client available */
		if (FD_ISSET(listSocket, &readSet)) {
			int newConnection;
			/* accept client connection */
			clientLen = sizeof(client_address);
			if ((newConnection = accept(listSocket, (struct sockaddr *) &client_address, &clientLen)) == -1) {
				printf("Error in accept: %s!\n", strerror(errno));
				return errno;
			}
			/* if maximum number of clients already connected */
			if (getClientsCount() >= MAX_NUM_OF_CLIENTS) {
				//printf("Only %d clients can be connected simultaneously!\n", MAX_NUM_OF_CLIENTS);
				if (rejectClient(newConnection)) {
					return 1; //exit on error
				}
			} else { /* if there are less than MAX_NUM_OF_CLIENTS */
				setNonblocking(newConnection);
				int clId;
				clId = getMaxId(clientList); /* check if there is available client ID (between 1 and 25) */
				/* try to find place for new connection listSocket */
				//printf("\nConnection accepted:   FD=%d; id=%d\n", newConnection, clId);
				/* if client ID is available and no more than MAX_NUM_OF_CLIENTS connected simultaneously */
				if (clId != CLIENT_ID_INVALID) { /* if client got ID set its parameters */
					clientList[clId] = (client_t *) malloc(sizeof(client_t));
					clientList[clId]->sock.socket = newConnection;
					clientList[clId]->sock.rxBuffPos = 0;
					clientList[clId]->sock.writeSet = &writeSet;
					clientList[clId]->status = (getPlayersCount() <= p) ? PLAYING : SPECTATOR; /* there are can be up to p players */
					if (getPlayersCount() > p) {
						clientList[clId]->status = SPECTATOR;
					}
					//printf("clientList[clId]->sock.socket=%d clientList[clId]->status=%d\n", clientList[clId]->sock.socket, clientList[clId]->status);
					//printf("Currently connected %d clients\n", getClientsCount());
					sendWelcomeMsg(&clientList[clId]->sock, clId, gameType, p, clientList[clId]->status);
					if (getCurrentPlayer() == NULL) {
						updateClientsStatus(&needToSendStatus);
						setNextPlayerAsCurrent();
					}
					payload_t* pl = (payload_t*) malloc(sizeof(payload_t));
					/* get heap state */
					int i;
					for (i = 0; i < NUM_OF_HEAPS; i++) {
						pl->status.heapStatus.heap[i] = heaps[i];
					}
					/* get client status */
					pl->status.clientStatus = clientList[clId]->status;
					/* check end of game */
					int isGameEnded = checkGameEnd(heaps);
					/* set end game status to client accordingly to game type */
					if (isGameEnded) {
						if (clientList[clId]->status == SPECTATOR) {
							pl->status.endGame = YOU_WATCHED;
						} else {
							pl->status.endGame = (gameType != MISERE) ? YOU_LOSE : YOU_WIN;
						}
					} else { /* if game not finished yet */
						pl->status.endGame = NOT_FINISHED;
					}
					/* send personal message with heap state */
					game_msg_t* personalHeapStatusMsg = createMessage(STATUS, *pl);
					if (!sendMessageB(&clientList[clId]->sock, personalHeapStatusMsg)) {
						onClientDisconnect(clientList[clId], &isTurnDone, &needToSendStatus);
					}
					destroyMsg(&(personalHeapStatusMsg));
				} else if (clId == CLIENT_ID_INVALID) { /* if invalid ID received by client*/
					//printf("Cann't accept connection! More than 25 players connected during one game!\n");
					if (rejectClient(newConnection)) { /* reject connection */
						return 1; //exit on error
					}
				}
			} /* playing client accepted */
		} /* handling listening socket */
		else { /* not listening socket */
			/* try to receive messages from active read ready socket */
			int id;
			for (id = 0; id < MAX_ID; id++) {
				client_t* client;
				client = clientList[id];
				if (client != NULL) {
					if (FD_ISSET(client->sock.socket, &readSet)) {
						game_msg_t* msg;
						int isDisconnect = 0;
						msg = receiveMessageB(&(client->sock), &isDisconnect);
						if (isDisconnect) {
							onClientDisconnect(client, &isTurnDone, &needToSendStatus);
						} else {
							if (msg != NULL) {
								handleMsg(msg, client, heaps, &isTurnDone, &needToSendStatus);
								destroyMsg(&msg);
							}
						}
						break; /* one client per cycle */
					}
				}
			}
			/* if turn done or need to send status */
			if (isTurnDone || needToSendStatus) {
				game_msg_t* statusMsg[MAX_ID];
				for (id = 0; id < MAX_ID; id++) {
					client_t* client;
					client = clientList[id];
					if (client != NULL) {
						payload_t* pl = (payload_t*) malloc(sizeof(payload_t));
						int i;
						for (i = 0; i < NUM_OF_HEAPS; i++) {
							pl->status.heapStatus.heap[i] = heaps[i];
						}
						pl->status.clientStatus = UNKNOWN;
						pl->status.endGame = NOT_FINISHED;
						statusMsg[id] = createMessage(STATUS, *pl);
					}
				}
				/* check if game is ended */
				int isGameEnded = checkGameEnd(heaps);
				if (isGameEnded) { /* game is ended - update end game status for all */
					client_t* lastPlayed = getCurrentPlayer();
					int lastPlayedSocket = (lastPlayed != NULL) ? lastPlayed->sock.socket : -1;
					int id;
					for (id = 0; id < MAX_ID; id++) {
						client_t* client;
						client = clientList[id];
						if (client != NULL) {
							if (client->status == SPECTATOR) {
								statusMsg[id]->payload.status.endGame = YOU_WATCHED;
							} else if (client->sock.socket == lastPlayedSocket) {
								statusMsg[id]->payload.status.endGame = (gameType == MISERE) ? YOU_LOSE : YOU_WIN;
							} else {
								statusMsg[id]->payload.status.endGame = (gameType != MISERE) ? YOU_LOSE : YOU_WIN;
							}
						}
					}
				} else { /* game is not ended - update client status for all */
					if (isTurnDone) {
						setNextPlayerAsCurrent();
					}
					int id;
					for (id = 0; id < MAX_ID; id++) {
						client_t* client;
						client = clientList[id];
						if (client != NULL) {
							//printf("client ID is %d, fd=%d, client status is %d\n", id, clientList[id]->sock.socket, client->status);
							statusMsg[id]->payload.status.clientStatus = client->status;
						}
					}
				}
				/* try to send messages to active write ready socket */
				for (id = 0; id < MAX_ID; id++) {
					client_t* client;
					client = clientList[id];
					if (client != NULL) {
						ALT(sendMessageB(&client->sock, statusMsg[id]), onClientDisconnect(client, &isTurnDone, &needToSendStatus));
						destroyMsg(&(statusMsg[id]));
					}
				}
			}
		}
		if (checkGameEnd(heaps)) {/* if no more cubes remains */
			/* exit when no more clients remain */
			if (getClientsCount() == 0) {
				break;
			}
		}
	} //while
	//close sockets
	int id;
	for (id = 0; id < MAX_ID; id++) {
		client_t* client;
		client = clientList[id];
		if (client != NULL) {
			if (close(client->sock.socket) == -1) {
				printf("Error in closing client #%d socket: %s!\n", id, strerror(errno));
			}
		}
	}
	if (close(listSocket) == -1) {
		printf("Error in closing listSocket: %s!\n", strerror(errno));
	}
	return 0; //end of program
}
