#ifndef _NETWORKS_H_
#define _NETWORKS_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>


#include "cpe464.h"

//Starting Sequence Number
#define START_SEQ_NUM 1

//Maximum of Times a packet is resent
#define MAX_TRIES 10

//Times to Select For
#define INSTANT_TIME 0
#define SHORT_TIME 1
#define LONG_TIME 10

//Minimum and Maximum Buffer Lengths
#define MIN_BUF_LEN 400
#define MAX_BUF_LEN 1400

//Minimum and Maximum Error Rates
#define MIN_ERR 0
#define MAX_ERR 1

//size of bufSize; Maximum Length of Buffer
#define SIZE_OF_BUF_SIZE 4
#define MAX_LEN 1500

//CRC Error for Bit Flips
#define CRC_ERROR -1


//Define flags
#define DATA_FLAG 1
#define RR_FLAG 3
#define SREJ_FLAG 4
#define REMOTE_FN_FLAG 6
#define FN_FLAG 7
#define END_OF_FILE 8
#define FN_BAD 10
#define FN_GOOD 11

enum SELECT { SET_NULL, NOT_NULL};

//Struct Declaration for a Connection
typedef struct connection Connection;
struct connection {
	int32_t sk_num;
	struct sockaddr_in remote;
	uint32_t len;
};

//Struct Declaration for a Window
typedef struct {
  uint32_t seqNum;
  int32_t buf_len;
  uint8_t flag;
  uint8_t buf[MAX_LEN];
} Window;


//Headers for Functions in networks.c
int32_t udpSetup (int portNum);
int32_t selectCall (int32_t socketNum, int32_t seconds, int32_t microseconds, int32_t setNull);
int32_t send_buf(uint8_t *buf, uint32_t len, Connection *connection, uint8_t flag, uint32_t seq_num, uint8_t *packet);
int32_t recv_buf(uint8_t *buf, int32_t len, int32_t recv_sk_num, Connection *connection, uint8_t *flag, int32_t *seq_num);
int processSelect(Connection * client, int *retryCount, int selectTimeoutState, int dataReadyState, int doneState);
int32_t udp_client_setup (char *hostname, uint16_t portNum, Connection *connection);
#endif
