#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <signal.h>

#include "Shared.h"

#define MAX_CLIENTS 50

#define MESG_SIZE 80
#define SA struct sockaddr

const int Distance = 'a'-'A';

int firstClientPort;

typedef _Atomic(uint64_t) atomic_uint64_t;

// Bitset of used client slots (and thus ports)
// 1 = Taken, 0 = Free
atomic_uint64_t freeCSlotBits = 0;

// Count trailing zeros
// if x == 0 returns 64
uint32_t safe_ctz(uint64_t x) {
	return x == 0 ? 64 : __builtin_ctzll(x);
}

bool reserveFreeCSlot(uint32_t* slot) {
	for (;;) {
		const uint64_t prev = atomic_load(&freeCSlotBits);
		uint32_t freeSlot = safe_ctz(~prev);
		
		if (freeSlot < MAX_CLIENTS) {
			uint64_t new = prev | (((uint64_t)0x1) << freeSlot);
			
			uint64_t expected = prev;
			if (atomic_compare_exchange_strong(&freeCSlotBits, &expected, new)) {
				*slot = freeSlot;
				return true;
			} else {
				// Try again
				continue;
			}
		} else {
			return false;
		}
	}
}

void freeCSlot(uint32_t slot) {
	assert(slot < MAX_CLIENTS);
	atomic_fetch_and(&freeCSlotBits, ~(((uint64_t)0x1) << slot));
}

typedef struct {
	int connfd;
	int slot;
} ClientData;

// Function designed for chat between client and server.
void changeCase(char* _str) {
	while (*_str != 0) {
		if(*_str >= 'a'  && *_str <= 'z') {
			*_str -= Distance;
		} else if(*_str >= 'A'  && *_str <= 'Z') {
			*_str += Distance;
		}
		_str++;
	}
}

void serverFunction(int sockfd) {
	char buffer[MESG_SIZE];
	
	// infinite loop for chat
	while (1) {
		// clear buffer
		bzero(buffer, MESG_SIZE);
		// read the message from client and copy it in buffer
		if (read(sockfd, buffer, sizeof(buffer)) <= 0) {
			printf("Failed to read client conn...\n");
			break;
		}
		
		// exchange upper-case letters by lower-case letter and vice versa.
		changeCase(buffer);
		
		// and send that buffer to client
		write(sockfd, buffer, sizeof(buffer));
		
		if (quitServer) {
			return;
		}
		
		//// if msg contains "QUIT" then server exit and chat ended.
		//if (strncmp("QUIT", buffer, 4) == 0) {
		//	printf("Server Exit...\n");
		//	break;
		//}
	}
}

/*
int setupConnection(int _port) {
	int sockfd, connfd, len;
	struct sockaddr_in servaddr, client;
	
	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		fprintf(stderr, "Error: Cannot create socket --- exit\n");
		exit(3);
	} else {
		printf("Socket created.\n");
	}
	bzero(&servaddr, sizeof(servaddr));
	
	// set up socket
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(_port);
	
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
		fprintf(stderr, "Error: Failed to set SO_REUSEADDR --- exit\n");
		exit(14);
	}
    
	// Binding socket to any IP
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
		fprintf(stderr, "Error: Cannot bind socket --- exit\n");
		exit(4);
	} else {
		printf("Socket bound.\n");
	}
	
	// Server listens
	if ((listen(sockfd, 5)) != 0) {
		fprintf(stderr, "Error: Cannot listen --- exit\n");
		exit(5);
	} else { 
		printf("Server listening.\n");
	}
	len = sizeof(client);

	// Accept data packet from client
	connfd = accept(sockfd, (SA*)&client, &len);
	if (connfd < 0) {
		fprintf(stderr, "Error: Server accept failed --- exit\n");
		exit(6);
	} else {
		printf("Server accept client.\n");
	}

	return connfd;
}
*/

int setupClientSocket(int* sockPort) {
	// socket create and verification
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		fprintf(stderr, "Error: Cannot create socket --- exit\n");
		exit(3);
	} else {
		printf("Socket created.\n");
	}
	
	// set up socket
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(0); // Bind to any free port
	
	// Binding socket to any IP
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
		fprintf(stderr, "Error: Cannot bind socket (setupClientConnection) --- exit\n");
		exit(4);
	} else {
		printf("Socket bound.\n");
	}
	
	// Server listens
	if ((listen(sockfd, 5)) != 0) {
		fprintf(stderr, "Error: Cannot listen --- exit\n");
		exit(5);
	} else { 
		printf("Server listening.\n");
	}
	
	// Get actual port of new socket
	struct sockaddr_in sin;
	socklen_t sinLen = sizeof(sin);
	getsockname(sockfd, (SA*)&sin, &sinLen);
	*sockPort = ntohs(sin.sin_port);
	
	return sockfd;
}

int finalizeClientConnection(int sockfd) {
	// Accept data packet from client
	SA client;
	socklen_t len = sizeof(SA);
	int connfd = accept(sockfd, (SA*)&client, &len);
	if (connfd < 0) {
		fprintf(stderr, "Error: Server accept failed --- exit\n");
		exit(6);
	} else {
		printf("Server accept client.\n");
	}
	return connfd;
}

int setupServerListenSocket(int port) {
	struct sockaddr_in servaddr;

	// Log
	printf("Setting up server socket on port %d\n", port);

	// socket create and verification
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		fprintf(stderr, "Error: Cannot create socket --- exit\n");
		exit(3);
	} else {
		printf("Socket created.\n");
	}
	bzero(&servaddr, sizeof(servaddr));
	
	// set up socket
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	//servaddr.sin_addr.s_addr = localServerAddr;
	servaddr.sin_port = htons(port);
	
	// Binding socket to any IP
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
		fprintf(stderr, "Error: Cannot bind server socket --- exit\n");
		exit(4);
	} else {
		printf("Socket bound.\n");
	}
	
	// Server listens
	if ((listen(sockfd, MAX_CLIENTS)) != 0) {
		fprintf(stderr, "Error: Cannot listen --- exit\n");
		exit(5);
	} else { 
		printf("Server listening.\n");
	}
	
	return sockfd;
}

void firstTouch(int sockfd, int connPort) {
	char buffer[MESG_SIZE];
	bzero(buffer, MESG_SIZE);
	read(sockfd, buffer, MESG_SIZE);
	printf("received: %s\n", buffer);
	((int*)buffer)[0] = connPort;
	write(sockfd, buffer, MESG_SIZE);
	close(sockfd);
}

void permantConnection(int slot, int connfd) {
	serverFunction(connfd);
	close(connfd);
	
	printf("Client #%d disconnected.\n", slot);
	
	// Clean up, free client slot
	freeCSlot(slot);
}

void* handleClientConnection(void* args) {
	ClientData* client = (ClientData*)args;
	
	permantConnection(client->slot, client->connfd);
	free(client);
	
	return NULL;
}

bool tryAcceptClient(int listenSockFd) {
	// Prereserve client slot
	uint32_t clientSlot;
	if (!reserveFreeCSlot(&clientSlot)) {
		// No client slots free
		return false;
	}
	
	// Accept new client
	socklen_t clientAddrLen;
	struct sockaddr clientAddr;
	
	/*
	// Select
	fd_set set;
	struct timeval timeout;
	FD_ZERO(&set); // clear the set
	FD_SET(listenSockFd, &set); // add our file descriptor to the set
	
	timeout.tv_sec = 0;
	timeout.tv_usec = 100000;
	
	for (;;) {
		int select_res = select(listenSockFd + 1, &set, NULL, NULL, &timeout);
		
		if (quitServer) {
			return false;
		}
		
		// Error occured
		if (select_res == -1) {
			fprintf(stderr, "Error: Server select failed --- exit\n");
			exit(6);
		}
		// Select timeout
		else if (select_res == 0) {
			// Continue selecting
		}
		// Select
		else {
			break;
		}
	}
	*/
	
	// Accept
	int connfd = accept(listenSockFd, (SA*)&clientAddr, &clientAddrLen);
	
	if (connfd < 0) {
		fprintf(stderr, "Error: Server accept failed --- exit\n");
		exit(6);
	} else {
		printf("Server accept client #%d.\n", clientSlot);
	}
	
	// Setup actual conn
	int laterPort;
	int laterSockFd = setupClientSocket(&laterPort);
	
	firstTouch(connfd, laterPort);
	
	int clientConnFd = finalizeClientConnection(laterSockFd);
	close(laterSockFd);
	
	ClientData* client = malloc(sizeof(ClientData));
	client->connfd = clientConnFd;
	client->slot = clientSlot;
	
	pthread_t clientThread;
	pthread_create(&clientThread, NULL, handleClientConnection, (void*)client);
	
	return true;
}

atomic_bool quitServer = false;

void* clientThreadMain(void* arg) {
	int clientId = (int)(size_t)arg;
	in_addr_t remoteAddr;
	int remotePort = globalRemotePort;
	
	printf("Local client thread started on #%d..\n", clientId+1);
	
	pthread_mutex_lock(&gmtx);
	remoteAddr = localClients[clientId].remoteAddr;
	pthread_mutex_unlock(&gmtx);
	
	//struct in_addr addr;
	//addr.s_addr = remoteAddr;
	//printf("DEBUG: Trying to connect to %s:%d\n", inet_ntoa(addr), remotePort);
	
	// First touch, get acquire port
	int newPort = clientFirstTouch(remoteAddr, remotePort);
	
	// Store real port into struct array
	pthread_mutex_lock(&gmtx);
	localClients[clientId].realPort = newPort;
	pthread_mutex_unlock(&gmtx);
	
	// Permanent connection func
	clientPermanentConnection(remoteAddr, newPort, clientId);
	
	return NULL;
}

/// clientId < 0 => next available
void connectNewClient(int clientId, in_addr_t remoteAddr) {
	assert(clientId <= 4);
	
	pthread_mutex_lock(&gmtx);
	
	if (clientId < 0) {
		// Search for next free client
		for (int i = 0; i < 5; i++) {
			if (!localClients[i].isActive) {
				clientId = i;
				break;
			}
		}
		
		// If still < 0, no client free
		if (clientId < 0) {
			fprintf(stderr, "No available connection\n");
			
			pthread_mutex_unlock(&gmtx);
			return;
		}
	}
	
	// Check if available
	if (localClients[clientId].isActive) {
		fprintf(stderr, "Connection on #%d already open\n", clientId);
		
		pthread_mutex_unlock(&gmtx);
		return;
	}
	
	// Create client info
	localClients[clientId].isActive = true;
	localClients[clientId].sendBuf = NULL;
	localClients[clientId].remoteAddr = remoteAddr;
	
	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	
	pthread_create(&localClients[clientId].clientThread, &attrs, clientThreadMain, (void*)(size_t)clientId);
	
	// ~~~~ Give up mutex, the client will reaquire it on it's own thread ~~~~
	pthread_mutex_unlock(&gmtx);
}

void printClientInfo() {
	printf("Connected clients:\n");
	
	pthread_mutex_lock(&gmtx);
	
	for (int i = 0; i < 5; i++) {
		if (localClients[i].isActive) {
			struct in_addr addr;
			addr.s_addr = localClients[i].remoteAddr;
			
			printf("#%d to %s:%d/%d\n", i+1, inet_ntoa(addr), globalRemotePort, localClients[i].realPort);
		}
		else {
			printf("#%d -\n", i+1);
		}
	}
	
	pthread_mutex_unlock(&gmtx);
}

void* uiThreadMain(__attribute__((unused)) void* _arg) {
	for (;;) {
		char inbuf[256];
		fgets(inbuf, sizeof(inbuf), stdin);
		int inbuf_num_read = strlen(inbuf);
		
		// Tokenize
		char* first = strtok(inbuf, " ");
		
		//if (first == NULL) {
		//	fprintf(stderr, "Unknown command\n");
		//	continue;
		//}
		
		// Send command
		if (strlen(first) == 1 && first[0] >= '1' && first[0] <= '5' && inbuf_num_read >= 2) {
			int clientId = (first[0] - '1');
			
			char* msg = inbuf + 2;
			char* sendbuf = malloc(strlen(msg));
			memcpy(sendbuf, msg, strlen(msg));
			
			pthread_mutex_lock(&gmtx);
			if (!localClients[clientId].isActive) {
				fprintf(stderr, "No open connection on #%d\n", clientId+1);
			} else {
				localClients[clientId].sendBuf = sendbuf;
			}
			pthread_mutex_unlock(&gmtx);
		}
		// Quit
		//else if (strcasecmp(inbuf, "Q") == 0) {
		else if (strlen(inbuf) >= 1 && (inbuf[0] == 'Q' || inbuf[0] == 'q')) {
			printf("[Quit command]\n");
			quitServer = true;
			
			// Last resort: Nuke the process
			raise(SIGTERM);
			
			return NULL;
		}
		// Connect
		else if (strcasecmp(first, "C") == 0) {
			printf("[Connect command]\n");
			
			char* ipstr = strtok(NULL, " ");
			
			in_addr_t remoteAddr = inet_addr(ipstr);
			
			connectNewClient(-1, remoteAddr);
		}
		// Disconnect
		else if (strcasecmp(first, "D") == 0) {
			printf("[Disconnect command]\n");
			
			char* clientIdStr = strtok(NULL, " ");
			
			if (clientIdStr == NULL) {
				fprintf(stderr, "Invalid client id, enter a number from 1 to 5\n");
				continue;
			}
			
			int clientIdPlusOne = atoi(clientIdStr);
			if (clientIdPlusOne < 1 || clientIdPlusOne > 5) {
				fprintf(stderr, "Invalid client id %d, enter a number from 1 to 5\n", clientIdPlusOne);
				continue;
			}
			int clientId = clientIdPlusOne-1;
			
			pthread_mutex_lock(&gmtx);
			
			if (!localClients[clientId].isActive) {
				fprintf(stderr, "No open connection to disconnect on #%d\n", clientId+1);
			} else {
				localClients[clientId].isActive = false;
			}
			
			pthread_mutex_unlock(&gmtx);
		}
		// Info
		else if (strlen(inbuf) >= 1 && (inbuf[0] == 'I' || inbuf[0] == 'i')) {
			printf("[Info command]\n");
			printClientInfo();
		}
		else {
			fprintf(stderr, "Unknown command\n");
		}
	}
	return NULL;
}

#define DEFAULT_SERVER_PORT 4567

int main(int argc, char* argv[]) {
	//int listen_port = 4567;
	//int listen_port = LOCAL_SERVER_PORT;
	//in_addr_t localServerIp = htonl(INADDR_ANY);
	
	int listen_port = DEFAULT_SERVER_PORT;
	int remote_port = DEFAULT_SERVER_PORT;
	
	//if (argc > 2) {
	//	fprintf(stderr, "usage: %s [port] --- exit\n", argv[0]);
	//	return 1;
	//}
	//if(argc == 2) {
	//	char* tmp = argv[1];
	//	while(tmp[0] != 0) {
	//		if(!isdigit((int)tmp[0])) {
	//			fprintf(stderr, "Error: %s is no valid port --- exit\n", argv[1]);
	//			return 2;
	//		}
	//		tmp++;
	//	}
	//	listen_port = atoi(argv[1]);
	//}
	
	// TODO: doesnt make sense
	if (argc < 1) {
		fprintf(stderr, "usage: %s [-ports localport remoteport] <ip_cl1?> <ip_cl2?> <ip_cl3?> <ip_cl4?> <ip_cl5?> --- exit\n", argv[0]);
		return 1;
	}
	
	int nextArg = 1;
	if (argc >= 4 && strcmp(argv[1], "-ports") == 0) {
		nextArg = 4;
		
		listen_port = atoi(argv[2]);
		remote_port = atoi(argv[3]);
		
		if (listen_port < 0 || remote_port < 0) {
			fprintf(stderr, "Error: Failed to parse port numbers");
			return 1;
		}
	}
	
	globalRemotePort = remote_port;
	
	//if (argc >= 3 && strcmp(argv[1], "-lip")) {
	//	localServerIp = htonl(inet_addr("192.168.1.2"));
	//	
	//	struct in_addr addr;
	//	addr.s_addr = localServerIp;
	//	printf("Using local server ip %s\n", inet_ntoa(addr));
	//}
	
	// Create global mutex
	pthread_mutex_init(&gmtx, NULL);
	
	//// Set first client port as listenport + 1
	//firstClientPort = listen_port + 1;
	
	// Setup server listen socket
	int listenSockFd = setupServerListenSocket(listen_port);
	
	// Start ui thread
	pthread_t ui_thread;
	pthread_create(&ui_thread, NULL, uiThreadMain, NULL);
	
	// Start clients
	for (int i = 0; i < 5; i++) {
		if (argc > nextArg + i) {
			char ipstr[16];
			bzero(ipstr, sizeof(ipstr));
			
			int len = 0;
			for (; len < (int)sizeof(ipstr)-1 && argv[nextArg + i][len] != ' ' && argv[nextArg + i][len] != '\x00'; len++) {}
			
			strncpy(ipstr, argv[nextArg + i], len);
			
			in_addr_t addr = inet_addr(ipstr);
			
			connectNewClient(i, addr);
		} else {
			break;
		}
	}
	
	for(;;) {
		if (quitServer) {
			printf("Exiting server listen loop...\n");
			break;
		}
		
		if (!tryAcceptClient(listenSockFd)) {
			printf("Cant connect any more clients!\n");
			// No slots free, wait 10 ms to try again
			usleep(10000);
		}
	}
	
	// Close server socket
	close(listenSockFd);
	
	// Close client sockets and join threads
	
	return 0;
}
