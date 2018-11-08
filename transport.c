#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* for read(), write() */
#include <sys/types.h> /* data types used in system calls */
#include <sys/socket.h> /* definitions of structures needed for sockets */
#include <netinet/in.h> /* constants and structures needed for Internet domain addresses */
#include <assert.h>
#include <errno.h> /* error messages */
#include <string.h> /* string functions */
#include "transport.h" /* common data with client */

/**
 * the function sends the message till there are no bytes remain
 * returns number of bytes sent and 0 on failure
 **/
ssize_t sendSafe(int sock_d, void * msg, size_t len) {
	ssize_t bytes_sent = 0;
	while (bytes_sent < len) {
		bytes_sent += send(sock_d, (ssize_t *) msg + bytes_sent, len - bytes_sent, 0);
	}
	if(bytes_sent == -1){
		return 0;
	}
	return bytes_sent;
}

/**
 * the function sends the message using sendSafe function
 **/
int sendMessage(int sock_d, game_msg_t * msg) {
	size_t msgSize = sizeof(game_msg_t);
	ssize_t bytes_sent;
	bytes_sent = sendSafe(sock_d, msg, msgSize);
	return bytes_sent == msgSize;
}

/**
 * the function sends message using buffer
 * returns 1 on success or 0 on failure
 **/
int sendMessageB(buffered_socket_t * socket, game_msg_t * msg) {
	if (msg != NULL) {
		size_t msgSize = sizeof(game_msg_t);
		if (socket->rxBuffPos + msgSize >= BUFFER_SIZE) {
			return 0;
		}
		memcpy(socket->rxBuff + socket->rxBuffPos, (ssize_t *) msg, msgSize);
		socket->rxBuffPos += msgSize;
	} else {
		if (socket->rxBuffPos == 0) {
			return 1;
		}
	}
	fd_set * writeSet = socket->writeSet;
	ssize_t bytes_sent = 0;
	while (1) {
		ssize_t sentNow = 0;
		if (FD_ISSET(socket->socket, writeSet)) {
			sentNow = send(socket->socket, socket->rxBuff + bytes_sent, socket->rxBuffPos - bytes_sent, 0);
		}
		if (sentNow == -1) {
			return 0;
		}
		if (sentNow == 0) {
			if (FD_ISSET(socket->socket, writeSet)){
				fprintf(stderr,"send failed while writeSet set\n");
			}
			memmove(socket->rxBuff, socket->rxBuff + bytes_sent, socket->rxBuffPos - bytes_sent);
			socket->rxBuffPos -= bytes_sent;
			break;
		}
		bytes_sent += sentNow;
		if (bytes_sent == socket->rxBuffPos) {
			socket->rxBuffPos = 0;
			break;
		}
	}
	return 1;
}

/**
 * the function receives the message till there are no bytes remain
 * returns number of bytes sent and 0 on failure
 **/
ssize_t recvSafe(int sock_d, void * out, size_t len) {
	ssize_t c = 0;
	char buff[len];
	while (c < len) {
		ssize_t rxNow = recv(sock_d, buff + c, len - c, 0);
		if (rxNow == -1 || rxNow == 0) {
			return -1;
		}
		c += rxNow;
	}
	out = memcpy(out, buff, len);
	return c;
}

/**
 * the function receives the message using recvSafe function
 * returns message received or NULL on failure
 **/
game_msg_t * receiveMessage(int sock_d) {
	size_t sizeType = sizeof(game_msg_t);
	game_msg_t * out = malloc(sizeType);
	ssize_t bytes_received;
	bytes_received = recvSafe(sock_d, out, sizeType);
	if (bytes_received != sizeType) {
		return NULL;
	}
	return out;
}

/**
 * the function receives message using buffer
 * returns message received or NULL on failure
 **/
game_msg_t * receiveMessageB(buffered_socket_t * socket, int * isDisconnect) {
	size_t msgSize = sizeof(game_msg_t);
	*isDisconnect=0;
	ssize_t rxTotal = 0;
	char buff[msgSize];
	while (rxTotal < msgSize) {
		ssize_t rxNow = recv(socket->socket, buff + rxTotal, msgSize - rxTotal, 0);
		if (rxNow == -1) {
			*isDisconnect = 1;
			return NULL;
		}
		if (rxNow == 0) {
			break;
		}
		rxTotal += rxNow;
	}
	memcpy(socket->txBuff + socket->txBuffPos, buff, rxTotal);
	socket->txBuffPos += rxTotal;
	if (socket->txBuffPos >= msgSize) {
		socket->rxAttempt=0;
		assert(socket->txBuffPos == msgSize);
		game_msg_t * out = malloc(msgSize);
		memcpy(out, socket->txBuff, msgSize);
		socket->txBuffPos -= msgSize;
		assert(socket->txBuffPos == 0);
		return out;
	}
	else if (socket->txBuffPos==0){
		socket->rxAttempt++;
		if (socket->rxAttempt>=RX_TIMEOUT){
			*isDisconnect = 1;
		}
	}
	return NULL;
}

/**
 * the function creates message of appropriate type
 * and returns it to caller
 **/
game_msg_t * createMessage(msgtype_t type, payload_t pl) {
	game_msg_t* msg = (game_msg_t*) malloc(sizeof(game_msg_t));
	msg->type = type;
	msg->payload = pl;
	return msg;
}

/**
 * the function frees the memory occupied by message
 * and sets the pointer of the message to NULL
 **/
void destroyMsg(game_msg_t ** msg) {
	free((*msg));
	*msg = NULL;
}

/**
* the function prints message in case of error
**/
void die(char * dyingMessage) {
	fprintf(stderr, "fatal error: %s\n", dyingMessage);
}


