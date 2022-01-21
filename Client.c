#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "Shared.h"

#define MESG_SIZE 80
#define SA struct sockaddr

static void clientFunction(int sockfd, int clientId) {
	char buffer[MESG_SIZE];
	
	while (1) {
		// Read new values
		pthread_mutex_lock(&gmtx);
		char* sendbuf = localClients[clientId].sendBuf;
		localClients[clientId].sendBuf = NULL;
		bool stillActive = localClients[clientId].isActive;
		pthread_mutex_unlock(&gmtx);
		
		if (!stillActive) {
			printf("Client state transition active -> not active. Exiting clientFunction\n");
			break;
		}
		
		//printf("Enter string: ");
		//n = 0;
		//while ((buffer[n++] = getchar()) != '\n')
		//	;
		
		if (sendbuf != NULL) {
			bzero(buffer, sizeof(buffer));
			memcpy(buffer, sendbuf, sizeof(buffer));
			
			if (write(sockfd, buffer, sizeof(buffer)) <= 0) {
				fprintf(stderr, "Failed to write() in clientFunction\n");
				break;
			}
			bzero(buffer, sizeof(buffer));
			if (read(sockfd, buffer, sizeof(buffer)) <= 0) {
				fprintf(stderr, "Failed to read() in clientFunction\n");
				break;
			}
			printf("Server response on #%d >> %s", clientId+1, buffer);
			
			//if ((strncmp(buffer, "QUIT", 4)) == 0) {
			//	printf("Client exit.\n");
			//	break;
			//}
			
			// Free sendbuf
			free(sendbuf);
		}
		else {
			// Check if still connected
			char buf[1];
			if (recv(sockfd, &buf, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
				printf("Connection to server was lost on #%d\n", clientId+1);
				break;
			}
			
			// No message to send, sleep for now
			usleep(10000);
		}
	}
	
	// Always disconnect client again
	pthread_mutex_lock(&gmtx);
	localClients[clientId].isActive = false;
	pthread_mutex_unlock(&gmtx);
}

static int clientSetupConnection(in_addr_t _serverAddress, int _port) {
	int sockfd;
	struct sockaddr_in servaddr;

	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		fprintf(stderr, "Error: Cannot create socket --- exit\n");
		return -3;
	} else {
		printf("  Socket successfully created..\n");
	}
	bzero(&servaddr, sizeof(servaddr));

	// Set up socket
	servaddr.sin_family = AF_INET;
	//servaddr.sin_addr.s_addr = htonl(_serverAddress);
	servaddr.sin_addr.s_addr = _serverAddress;
	servaddr.sin_port = htons(_port);
	
	// Connect to server
	printf("  Trying to connect to server... (might time out in 30 seconds)\n");
	if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
		fprintf(stderr, "Error: Cannot connect to server --- exit\n");
		return -4;
	} else {
		printf("  Connected to the server!\n");
	}

	return sockfd;
}

int clientFirstTouch(in_addr_t _serverAddress, int _port) {
	int sockfd = clientSetupConnection(_serverAddress, _port);
	if(sockfd < 0)
		exit(-1*sockfd);
	char buffer[MESG_SIZE];
	bzero(buffer, MESG_SIZE);
	strcpy(buffer, "HelloServer");
	write(sockfd, buffer, MESG_SIZE);
	bzero(buffer, MESG_SIZE);
	read(sockfd, buffer, MESG_SIZE);
	_port = ((int*)buffer)[0];
	close(sockfd);
        return _port;
}

void clientPermanentConnection(in_addr_t _serverAddress, int _port, int clientId) {
	int sockfd = clientSetupConnection(_serverAddress, _port);
	int retries = 5;
	while(sockfd < 0 && retries > 0) {
		sleep(1);
		sockfd = clientSetupConnection(_serverAddress, _port);
		retries--;
	}
	if(retries == 0 && sockfd < 0) {
		fprintf(stderr, "Error: Cannot reconnect server --- exit\n");
		exit(-1*sockfd);
	}
	clientFunction(sockfd, clientId);
	close(sockfd);
}

/*
int main(int argc, char** argv) {
	char* serverAddress = "127.0.0.1";
	int port = 4567;
	
	if(argc > 3) {
		fprintf(stderr, "usage: %s [server [port]] --- exit\n", argv[1]);
		return 1;
	}
	if(argc == 2) {
		serverAddress = argv[1];
    }
	if(argc == 3) {
		char* tmp = argv[2];
        serverAddress = argv[1];
        while(tmp[0] != 0) {
            if(!isdigit((int)tmp[0])) {
                fprintf(stderr, "Error: %s is no valid port --- exit\n", argv[1]);
                return 2;
            }
            tmp++;
        }
        port = atoi(argv[2]);
	}

	port = firstTouch(serverAddress, port);
	permanentConnection(serverAddress, port);

	return 0;
}
*/
