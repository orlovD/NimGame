#define MAX_CHAT_TEXT (60) /* maximal text message length from client to client */
#define BUFFER_SIZE (1024) /* maximal output and input buffer size */
#define RX_TIMEOUT (3) /* number of sending attempts if socket returns 0 */

static const char CLIENT_ID_INVALID = -1; /* invalid client ID */

/**
 * definition of message types:
 * WELCOME - message from server to recently connected client
 * 			 contains gameType, playersCnt, clientId and clientStatus
 * STATUS - message from server with current game status
 * 			contains heapStatus, clientStatus and endGame status
 * TURN_REQ - message from client to server with new move received from a user
 * 			  message contains heapIndex and amount of cubes to be taken from that heap
 * TURN_RESP - message with server response to the move received from client
 * 			   response can be that move is LEGAL or ILLEGAL or NOT_YOUR_TURN
 * CHAT - chat message from client to client
 * 		  contains srcId, dstId and text
 **/
typedef enum {
	WELCOME, STATUS, TURN_REQ, TURN_RESP, CHAT
} msgtype_t;

/**
 * definition of game types:
 * MISERE - looser of game is player who takes last cube from heap
 * REGULAR - winner of game is player who takes last cube from heap
 * REJECTED - connect attempt not succeeded
 **/
typedef enum {
	MISERE, REGULAR, REJECTED
} game_type_t;

/**
 * definition of client status:
 * PLAYING - client is player in current game
 * SPECTATOR - client is spectator in current game
 * YOUR_TURN - client can make move now
 * UNKNOWN - status not determined yet
 **/
typedef enum client_status {
	PLAYING, SPECTATOR, YOUR_TURN, UNKNOWN
} client_status_t;

/**
 * definition of move types
 * LEGAL - move accepted by the server
 * NOT_YOUR_TURN - turn is out of player's order
 * ILLEGAL - move rejected by the server (invalid move)
 **/
typedef enum {
	LEGAL, NOT_YOUR_TURN, ILLEGAL
} turn_resp_t;

/**
 * definition of end game statuses
 * YOU_WIN - player win
 * YOU_LOSE - player lose
 * YOU_WATCHED - player watched
 * NOT_FINISHED - game not finished yet
 **/
typedef enum {
	YOU_WIN, YOU_LOSE, YOU_WATCHED, NOT_FINISHED
} end_game_t;

/**
 * welcome message data
 * gameType - current game type of game_type_t type, can be one of defined game types
 * playersCnt - number of players (p) in current game
 * clientId - ID received by client
 * clientStatus - current client status of client_status_t, can be one of defined client statuses
 **/
typedef struct welcome_msg {
	game_type_t gameType;
	char playersCnt;
	char clientId;
	client_status_t clientStatus;
} welcome_msg_t;

/**
 * heap data
 * heap[4] - current state of the heaps
 **/
typedef struct heap_status {
	short heap[4];
} heap_status_t;

/**
 * status data
 * heapStatus - of heap_status_t type, contains current state of heaps array
 * clientStatus - current client status
 * endGame - current game state
 **/
typedef struct status {
	heap_status_t heapStatus;
	client_status_t clientStatus;
	end_game_t endGame;
} status_t;

/**
 * user move data
 * heapIndex - index of a heap chosen by user
 * amount - amount of cubes to take from chosen heap
 **/
typedef struct turn_req {
	char heapIndex;
	short amount;
} turn_req_t;

/**
 * chat message data
 * srcId - sender ID
 * dstId - receiver ID
 * text - message data
 **/
typedef struct chat {
	char srcId;
	char dstId;
	char text[MAX_CHAT_TEXT];
} chat_t;

/**
 * message data - can be read as one of five types
 * accordingly to the message type
 **/
typedef union payload {
	chat_t chat;
	welcome_msg_t welcomeMsg;
	status_t status;
	turn_req_t turnReq;
	turn_resp_t turnResp;
} payload_t;

/**
 * structure for the message in the system
 * type - type of the message, can be one of four types defined by msgtype_t
 * payload - relevant data accordingly to the message type
 **/
typedef struct game_msg {
	msgtype_t type;
	payload_t payload;
} game_msg_t;

/**
 * structure for buffered socket
 * socket - socket fd
 * rxBuff - input buffer
 * rxBuffPos - current place in input buffer
 * rxAttempt - number of read attempts done
 * txBuff - output buffer
 * txBuffPos - current place in output buffer
 * writeSet - pointer to write-ready set
 **/
typedef struct buffered_socket{
	int socket;
	char rxBuff[BUFFER_SIZE];
	int rxBuffPos;
	int rxAttempt;
	char txBuff[BUFFER_SIZE];
	int txBuffPos;
	fd_set * writeSet;
}buffered_socket_t;

/* headers of common functions */
game_msg_t * createMessage(msgtype_t, payload_t);

int sendMessage(int sock_d, game_msg_t * msg);

int sendMessageB(buffered_socket_t * socket, game_msg_t * msg);

game_msg_t * receiveMessage(int sock_d);

game_msg_t * receiveMessageB(buffered_socket_t * socket,int * isDisconnect);

void destroyMsg(game_msg_t ** msg);

void die(char * dyingMessage);


