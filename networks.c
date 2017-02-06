/* 
 * Helper Functions used by both rCopy and server.
 * A large part of this code was provided by 
 * Professor Smith. 
 */
#include "cpe464.h"
#include "networks.h"


int32_t udpSetup (int portNum) {
	int sk = 0;
	struct sockaddr_in local;
	uint32_t size = sizeof(local);

	if ((sk = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		exit(-1);
	}

	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(portNum);

	if (bindMod(sk, (struct sockaddr *) &local, sizeof(local)) < 0) {
		perror("udp -> bind");
		exit(-1);
	}

	getsockname(sk, (struct sockaddr *) &local, &size);
	printf("Using Port Number: %d\n", ntohs(local.sin_port));

	return (sk);
}

int32_t udp_client_setup (char *hostname, uint16_t portNum, Connection *connection) {
	struct hostent *hp = NULL;

	connection->sk_num = 00;
	connection->len = sizeof(struct sockaddr_in);

	if ((connection->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("udp_client_setup, socket");
		exit(-1);
	}
	connection->remote.sin_family = AF_INET;
	hp = gethostbyname(hostname);

	if (hp == NULL) {
		printf("Host not found: %s\n", hostname);
		return -1;
	}

	memcpy(&(connection->remote.sin_addr), hp->h_addr, hp->h_length);
	connection->remote.sin_port = htons(portNum);

	return 0;
}

int32_t selectCall (int32_t socketNum, int32_t seconds, int32_t microseconds, int32_t setNull) {
	fd_set fdvar;
	struct timeval aTimeout;
	struct timeval *timeout = NULL;
	if (setNull == NOT_NULL) {
		aTimeout.tv_sec = seconds;
		aTimeout.tv_usec = microseconds;

		timeout = &aTimeout;
	}

	FD_ZERO(&fdvar);
	FD_SET(socketNum, &fdvar);

	if (selectMod(socketNum + 1, (fd_set *) &fdvar, (fd_set *) 0, (fd_set *) 0, timeout) < 0) {
		printf("shouldn't be here.\n");
		perror("select");
		exit(-1);
	}
	if (FD_ISSET(socketNum, &fdvar)) {
		return 1;
	}
	else {
		return 0;
	}
}

int processSelect(Connection *client, int *retryCount, int selectTimeoutState, int dataReadyState, int doneState) {
	int returnValue = dataReadyState;

	(*retryCount)++;
	if (*retryCount > MAX_TRIES) {
		printf("Sent data %d times, no ACK, client is probably gone, so I'm terminating\n", MAX_TRIES);
		returnValue = doneState;
	}
	else {
		if (selectCall(client->sk_num, SHORT_TIME, 0, NOT_NULL) == 1) {
			*retryCount = 0;
			returnValue = dataReadyState;
		}
		else {
			returnValue = selectTimeoutState;
		}
	}

	return returnValue;
}

int32_t send_buf(uint8_t *buf, uint32_t len, Connection *connection, uint8_t flag, uint32_t seq_num, uint8_t *packet) {
	int32_t sentLen = 0;
	uint16_t checksum = 0;
	if (len > 0) {
		memcpy(&packet[7], buf, len);
	}
	
	seq_num = htonl(seq_num);
	memcpy(&packet[0], &seq_num, sizeof(uint32_t));
	memset(&packet[4], 0, 2);
	packet[6] = flag;
	
	checksum = in_cksum((unsigned short *)packet, len + 8);
	memcpy(&packet[4], &checksum, 2);
	
	if ((sentLen = sendtoErr(connection->sk_num, packet, len+8, 0, 
		(struct sockaddr *) &(connection->remote), connection->len)) < 0) {
		perror("send_buf, sendto");
		exit(-1);
	}
	return sentLen;
}

int32_t recv_buf(uint8_t *buf, int32_t len, int32_t recv_sk_num, Connection *connection, uint8_t *flag, int32_t *seq_num) {
	char data_buf[MAX_LEN];
	int32_t recv_len = 0;
	uint32_t remoteLen = sizeof(struct sockaddr_in);
	if((recv_len = recvfromErr(recv_sk_num, data_buf, len, 0, 
		(struct sockaddr *) &(connection->remote), &remoteLen)) < 0) {
		perror("Recv_buf, recvFrom");
		exit(-1);
	}
	connection->len = remoteLen;
	if (in_cksum((unsigned short *)data_buf, recv_len) != 0) {
		return CRC_ERROR;
	}
	else {
	   *flag = data_buf[6];
	   memcpy(seq_num, data_buf, 4);
	   *seq_num = ntohl(*seq_num);
	   
		if (recv_len > 7) {
			memcpy(buf, &data_buf[7], recv_len - 8);
		}
	}
	return (recv_len - 8);
}