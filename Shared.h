#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define MAX_LOCAL_CLIENTS 5

typedef struct {
	pthread_t clientThread;
	bool isActive;
	char* sendBuf;
	in_addr_t remoteAddr;
	uint16_t realPort;
	//atomic_bool isActive;
	//_Atomic char* sendBuf;
} ClientInfo;

pthread_mutex_t gmtx;
ClientInfo localClients[MAX_LOCAL_CLIENTS];
atomic_bool quitServer;

typedef _Atomic(uint16_t) atomic_u16_t;

atomic_u16_t globalRemotePort;

int clientFirstTouch(in_addr_t _serverAddress, int _port);
void clientPermanentConnection(in_addr_t _serverAddress, int _port, int clientId);
