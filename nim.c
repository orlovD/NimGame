#include<stdio.h>
#include<stdlib.h>
#include <unistd.h> /* for read(), write() */
#include <sys/types.h> /* data types used in system calls */
#include <sys/socket.h> /* definitions of structures needed for sockets */
#include <netinet/in.h> /* constants and structures needed for Internet domain addresses */
#include <netdb.h> /* for gethostbyname() */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h> /* error messages */
#include <string.h> /* string functions */
#include <strings.h> /* string functions */
#include "transport.h" /* common data with client */
#include <sys/select.h> /* select */

#define LOCALHOST "127.0.0.1"
#define DEFAULT_HOSTNAME LOCALHOST
#define DEFAULT_PORT 6325

int spect = 0; //if client is spectator
int clID = 0; //client ID
game_msg_t * INVALID_TURN_MSG;

/**
 * the function prints states of the heaps in the heaps array
 **/
void printHeapState(short int* heap) {
	printf("Heap sizes are %d, %d, %d, %d\n", heap[0], heap[1], heap[2], heap[3]);
}

/**
 * the function processed turn response message received from server
 * the function prints accept message if the move accepted by the server
 * and reject message if the move was illegal and rejected by the server
 **/
void processTurnResponse(turn_resp_t move) {
	if (move == ILLEGAL) {
		printf("Illegal move\n");
	} else if (move == LEGAL) {
		printf("Move accepted\n");
	} else if (move == NOT_YOUR_TURN) {
		printf("Move rejected: this is not your turn\n");

	}
}

/**
 * the function gets input from user
 * it checks for valid structure of the input
 * returns message with user input - can be chat or move
 * if user entered Q - sets doExit to 1
 * returns NULL on invalid input
 **/
game_msg_t * getPlayerInput(int * doExit) {
	game_msg_t* out;
	char line[1024], line2[1024];
	fgets(line, sizeof(line), stdin);
	/* check if it is message */
	if (strstr(line, "MSG ") == line) {
		*doExit = 0;
		char text[MAX_CHAT_TEXT];
		int clientId;
		sscanf(line, "MSG %d %s", &clientId, text);
		sprintf(line2, "MSG %d ", clientId);
		if (strstr(line, line2) != line) {
			return NULL;
		}
		strcpy(text, line + strlen(line2));
		if (text[strlen(text)-1]=='\n'){
			text[strlen(text)-1]='\0';
		}
		/* if it is message */
		out = (game_msg_t *) malloc(sizeof(game_msg_t));
		out->type = CHAT;
		out->payload.chat.dstId = clientId;
		strcpy(out->payload.chat.text, text);
		out->payload.chat.srcId = clID;
	}
	/* user asked for exit */
	else if(!strcmp(line,"Q")||!strcmp(line,"Q\n")) {
		*doExit=1;
		return NULL;
	}
	/* if it is not a message */
	else {
		*doExit=0;
		char heap;
		int cubes;
		sscanf(line, "%c %d", &heap, &cubes);
		sprintf(line2, "%c %d", heap, cubes);
		if (strstr(line, line2) != line) {
			return NULL;
		}
		out = (game_msg_t *) malloc(sizeof(game_msg_t));
		out->type = TURN_REQ;
		out->payload.turnReq.heapIndex = (heap) - 'A';
		out->payload.turnReq.amount = cubes;
	}
	return out;
}

/**
 * the function executes the client part of the game
 * checks if stdin or server socket ready and act accordingly
 * returns 0 if there are any errors
 * returns 1 on success, when game finished and the winner defined
 * returns 2 if user asked to quit
 **/
int runGameClient(int clienSocket, end_game_t * winner) {
	fd_set readSet; /* set of read-ready socket file descriptors for select */
	fd_set writeSet; /* set of read-ready socket file descriptors for select */
	FD_ZERO(&readSet); /* initialize set of read-ready sockets */
	FD_ZERO(&writeSet); /* initialize set of write-ready sockets */
	/* game cycle */
	while (1) {
		int highSD = clienSocket; /* highest socket descriptor */
		FD_SET(clienSocket, &readSet); /* add clienSocket socket to read-ready set */
		FD_SET(0, &readSet); /* add stdin to read-ready set */
		/* Number of sockets ready for reading */
		select(highSD + 1, &readSet, (fd_set *) 0, (fd_set *) 0, NULL);
		/* clienSocket socket is read-ready - new message is available */
		if (FD_ISSET(clienSocket, &readSet)) {
			game_msg_t* resp = receiveMessage(clienSocket);
			if (resp == NULL) {
				printf("Error in receiving message!\n");
				//die("receiveMessage");
				return 0;
			}
			if (resp->type == TURN_RESP) {
				processTurnResponse(resp->payload.turnResp);
			} else if (resp->type == CHAT) {
				printf("%d: %s\n", resp->payload.chat.srcId, resp->payload.chat.text);
			} else if (resp->type == STATUS) {
				printHeapState(resp->payload.status.heapStatus.heap);
				/* this client was spectator */
				if (spect == 1 && resp->payload.status.clientStatus != SPECTATOR && resp->payload.status.endGame == NOT_FINISHED) {
					printf("You are now playing!\n");
					spect = 0; /* now playing */
				}
				if (resp->payload.status.clientStatus == YOUR_TURN) {
					printf("Your turn:\n");
				} else if (resp->payload.status.endGame != NOT_FINISHED) {
					*winner = resp->payload.status.endGame;
					return 1;
				}
				destroyMsg(&resp);
			}
		}
		/* stdin is read-ready - new input is available */
		if (FD_ISSET(0, &readSet)) {
			int doExit = 0;
			game_msg_t * msg;
			msg = getPlayerInput(&doExit);
			if (doExit) {
				return 2;
			}
			if (!sendMessage(clienSocket, (msg != NULL) ? msg : INVALID_TURN_MSG)) {
				printf("Error in sending message!\n");
				//die("sendTurnMessage");
				return 0;
			}
			destroyMsg(&msg);
		}
	} //end while
	return 0;
}

int main(int argc, char *argv[]) {
	int clienSocket;
	int port = DEFAULT_PORT; /* default port */
	char *inetAddr = LOCALHOST; /* default address */
	struct sockaddr_in server_address; /* structure for socket parameters */
	struct hostent *server; /* defines a host computer on the Internet */
	/* check for arguments received in the command line */
	if (argc == 1 || argc == 2 || argc == 3) { /* if there are 0, 1 or 2 command line arguments */
		if (argc == 2) { /* host name received */
			inetAddr = argv[1];
		}
		if (argc == 3) { /* host name and port received */
			inetAddr = argv[1];
			port = atoi(argv[2]);
		}
	} else {
		printf("Error: Wrong number of arguments received!\n");
		return 1;
	}
	/* create listening socket */
	if ((clienSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) { /* create socket of Internet domain, messages read in streams, OS chooses TCP */
		printf("Error creating socket: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	/* returns a pointer to a structure w/ an information about host */
	if ((server = gethostbyname(inetAddr)) == NULL) {
		printf("Error: No server with such a name exists!\n");
		return 1; //exit on error
	}

	bzero((char *) &server_address, sizeof(server_address));
	server_address.sin_family = AF_INET; /* code for the address family, always set to the AF_INET */
	bcopy((char *) server->h_addr, (char *)&server_address.sin_addr.s_addr, server->h_length);
	server_address.sin_port = htons(port); /* set default port */
	/* connect to the server */
	if (connect(clienSocket, (struct sockaddr *) &server_address, sizeof(server_address))) { /* connect from socket w/ file descriptor socket to socket w/ address specified by address and length arguments */
		printf("Error connection to server: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	/* receive message from server */
	game_msg_t* gameType = receiveMessage(clienSocket);
	if (gameType != NULL) {
		assert(gameType->type == WELCOME);
		//if connected rejected by server
		if (gameType->payload.welcomeMsg.gameType == REJECTED) {
			printf("Client rejected: too many clients are already connected\n"); /* print reject message */
			if ((close(clienSocket) == -1)) { //close connection
				printf("Error closing connection: %s!\n", strerror(errno));
				return 1; //exit on error
			}
			return 1; //exit after connection reject
		}
		/* print welcome message data */
		printf("This is a %s game\n", (gameType->payload.welcomeMsg.gameType == MISERE) ? "Misere" : "Regular"); /* print game type */
		printf("Number of players is %d\n", gameType->payload.welcomeMsg.playersCnt); /* print number of players */
		printf("You are client %d\n", gameType->payload.welcomeMsg.clientId + 1); /* print client ID */
		clID=gameType->payload.welcomeMsg.clientId + 1;
		if (gameType->payload.welcomeMsg.clientStatus == PLAYING) { /* print client status */
			printf("You are playing\n");
		} else {
			printf("You are only viewing\n");
			spect = 1; /* this client is spectator */
		}
		/* create invalid turn message */
		INVALID_TURN_MSG = (game_msg_t *) malloc(sizeof(game_msg_t));
		INVALID_TURN_MSG->type = TURN_REQ;
		INVALID_TURN_MSG->payload.turnReq.heapIndex = 'Z';
		INVALID_TURN_MSG->payload.turnReq.amount = -1;
		/* check winner */
		end_game_t winner;
		int result = runGameClient(clienSocket, &winner);
		switch (result) {
		case (0):
			printf("Disconnected from server\n");
			break;
		case (1):
			switch (winner) {
				case YOU_WIN:
					printf("You win!\n");
					break;
				case YOU_LOSE:
					printf("You lose!\n");
					break;
				case YOU_WATCHED:
					printf("Game over!\n");
					break;
				default: {
				}
			}
			break;
		case (2):
			/*on quit dies silently*/
			break;
		}
	} else {
		printf("Disconnected from server\n");
	}
	destroyMsg(&INVALID_TURN_MSG);
	/* close client's socket */
	if (close(clienSocket) == -1) {
		printf("Error in closing socket: %s!\n", strerror(errno));
		return errno; //exit on error
	}
	return 0; /* end of program */
}
