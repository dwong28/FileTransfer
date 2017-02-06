#include "networks.h"
#include "cpe464.h"

#define MAX_ARGS 8
#define MAX_FILENAME_LEN 100

//Enum Declaration for State Differentiation
typedef enum State STATE;
enum State {
	START, FILENAME, DONE, SEND_RM_FILE, SEND_DATA, WIN_CLOSED, END_DATA
};

//Function Headers
void checkArgs(int argc, char **argv);
void cycleState(STATE state, char *argv[], int32_t outputFileDes, Connection server);
STATE startState (char **argv, Connection *server);
STATE fileName(int *outputFileDes, char *filename);
STATE remoteFileName (char *filename, int32_t bufSize, int32_t windowSize, Connection *server);
int32_t loadData (Window *winBuf, int32_t dataFile, int32_t windowSize, int32_t bufSize, uint32_t *seqNum);
STATE sendData(Window *winBuf, int32_t windowSize, Connection *connection, int32_t index, int32_t *bottomEdge, int32_t *upperEdge);
void updateWindow (int32_t windowSize, int32_t *bottomEdge, int32_t *upperEdge, uint32_t ackNum);
uint32_t getAck(Connection *connection, uint32_t *ack);
STATE winClosed (Connection *connection, int32_t *bottomEdge, int32_t *upperEdge, Window *windowBuf, int32_t windowSize, uint32_t *resendCnt);
STATE lastPacket (Window *windowBuf, int32_t windowSize, Connection *connection, int32_t *bottomEdge, int32_t *upperEdge, uint32_t *lastCnt);


int main(int argc, char * argv[]) {
	Connection server;
	int32_t outputFileDes = 0;
	STATE state = START;

	checkArgs(argc, argv);
	sendErr_init(atof(argv[4]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
	cycleState(state, argv, outputFileDes, server);
	return 0;
}

//Process Arguments to check for their Validity
void checkArgs(int argc, char **argv) {
	if (argc != MAX_ARGS) {
		printf("Usage %s fromFile toFile bufferSize errorRate windowSize shostName port\n", argv[0]);
		exit(-1);
	}
	if (strlen(argv[1]) > MAX_FILENAME_LEN) {
		printf("FROM file name too long must be within 100 and is: %d\n", strlen(argv[1]));
		exit(-1);
	}
	if (strlen(argv[2]) > MAX_FILENAME_LEN) {
		printf("TO file name too long must be within 100 and is: %d\n", strlen(argv[2]));
		exit(-1);
	}
	if (atoi(argv[3]) < MIN_BUF_LEN || atoi(argv[3]) > MAX_BUF_LEN) {
		printf("Invalid buffer. (Must be between 400 and 1400) Input Buffer: %d\n", atoi(argv[3]));

	}
	if (atof(argv[4]) < MIN_ERR || atof(argv[4]) >= MAX_ERR){
		printf("Invalid error Rate. (Must be between 0 and 1) Input Error: %f\n", atof(argv[4]));
		exit(-1);
	}
}

//Cycle through various States
void cycleState(STATE state, char *argv[], int32_t outputFileDes, Connection server) {
	STATE curState = state;
	int32_t fromFile = 0;
	int32_t bufSize = atoi(argv[3]);
	int32_t windowSize = atoi(argv[5]), bottomEdge = 1;
   int32_t upperEdge = bottomEdge + windowSize;
   int index = 0;
   Window *winBuf = malloc(sizeof(Window) * windowSize);
   uint32_t seqNum = 1, resendCnt = 0, lastCnt = 0;
	while (curState != DONE) {
		switch (curState) {
			case START:	
				//Initial State
				curState = startState(argv, &server);
				break;
			case FILENAME: 
				//Locate and open local file for reading
				curState = fileName(&fromFile, argv[1]);
				break;
			case SEND_RM_FILE:	
				//Locate/Create remote file for writing
				curState = remoteFileName(argv[2], atoi(argv[3]), windowSize, &server);
				break;
			case SEND_DATA:	
				//Send Data
				if (seqNum < upperEdge) {
					//Window Open
					index = loadData(winBuf, fromFile, windowSize, bufSize, &seqNum);
					curState = sendData(winBuf, windowSize, &server, index, &bottomEdge, &upperEdge);
				}
				else {
					//Window Closed
               		printf("Window closed.\n");
					curState = WIN_CLOSED;
				}
				break;
			case WIN_CLOSED:	
				//Window Closed, Resend packet
				curState = winClosed(&server, &bottomEdge, &upperEdge, winBuf, windowSize, &resendCnt);
				if (resendCnt == MAX_TRIES) {
					//Packet lost 10 times.
					printf("Sent 10 times. Terminating.\n");
					curState = DONE;
				}
				break;
			case END_DATA:	
				//Last Packet in File
				curState = lastPacket(winBuf, windowSize, &server, &bottomEdge, &upperEdge, &lastCnt);
				if (lastCnt == MAX_TRIES) {
					//Packet was lost 10 times.
					printf("Sent last packet 10 times, Terminating.\n");
					curState = DONE;
				}
				break;
			case DONE:	
				//Done. Terminate
				break;
			default:
				//Should Never Get Here
				printf("Error - in default state\n");
				break;
		}
	}
}

STATE startState (char **argv, Connection *server) {
	STATE returnValue = FILENAME;
	//If server connection was made previously, close the connection first
	if (server->sk_num > 0) {
		close(server->sk_num);
	}

	if (udp_client_setup(argv[6], atoi(argv[7]), server) < 0) {
		
		returnValue = DONE;
	}
	else {
		returnValue = FILENAME;
	}

	return returnValue;
}

//Open Local File
STATE fileName(int *fromFile, char *filename) {
	STATE returnValue = DONE;

	if ((*fromFile = open(filename, O_RDONLY)) < 0) {
		printf("Error opening Local File: %s\n", filename);
		returnValue = DONE;
	}
	else {
		returnValue = SEND_RM_FILE;
	}
	return returnValue;
}

//Send Requested Remote File to Server
STATE remoteFileName (char *filename, int32_t bufSize, int32_t windowSize, Connection *server) {
	STATE returnValue = SEND_RM_FILE;
	uint8_t packet[MAX_LEN];
	uint8_t buf[MAX_LEN];
	uint8_t flag = 0;
	int32_t seqNum = 0;
	int32_t nameLength = strlen(filename) + 1;
	int32_t recv_check = 0;
	static int retryCnt = 0;

	bufSize = htonl(bufSize);

	memcpy(buf, &bufSize, SIZE_OF_BUF_SIZE);
	memcpy(&buf[4], &windowSize, 4);
	memcpy(&buf[8], filename, nameLength);
	send_buf(buf, nameLength+8, server, REMOTE_FN_FLAG, 0, packet);

	if ((returnValue = processSelect(server, &retryCnt, SEND_RM_FILE, FN_GOOD, DONE)) == FN_GOOD) {
		recv_check = recv_buf(packet, MAX_LEN, server->sk_num, server, &flag, &seqNum);

		if (recv_check == CRC_ERROR) {
			printf("CRC_ERROR\n");
			returnValue = START;
		}
		else if (flag == FN_BAD) {
			printf("Error during file open of %s on server.\n", filename);
			returnValue = DONE;
		}
		else {
			returnValue = SEND_DATA;
		}
	}
	return (returnValue);
}

//Load Data from Local File into Window
int32_t loadData (Window *winBuf, int32_t dataFile, int32_t windowSize, int32_t bufSize, uint32_t *seqNum) {
	int32_t readLen = 0;
	int index = *seqNum % windowSize;
	if ((readLen = read(dataFile, winBuf[index].buf, bufSize)) <  0) {
		perror("read Error");
		exit(-1);
	}
	else if (readLen != bufSize) {
		//Data doesn't fill up buffer ==> EOF
		winBuf[index].seqNum = *seqNum;
		winBuf[index].buf_len = readLen;
		winBuf[index].flag = END_OF_FILE;
	}
	else {
		//Data fills up buffer
		winBuf[index].seqNum = *seqNum;
		winBuf[index].buf_len = readLen;
		winBuf[index].flag = DATA_FLAG;
		(*seqNum)++;
	}
	return index;
}

//Sends Packet, then checks if any thing from Server
STATE sendData(Window *winBuf, int32_t windowSize, Connection *connection, int32_t index, int32_t *bottomEdge, int32_t *upperEdge) {
	uint8_t packet[MAX_LEN] = {0}, ackFlag = 0;
	uint32_t ack, resendPacket;

	send_buf(winBuf[index].buf, winBuf[index].buf_len, connection, winBuf[index].flag, winBuf[index].seqNum, packet);

	if (winBuf[index].flag == END_OF_FILE) {
		//Sent Last Packet, go to END_DATA State
		return END_DATA;
	}

	//Non blocking Select
	if (selectCall(connection->sk_num, INSTANT_TIME,0,1)) {
		ackFlag = getAck(connection, &ack);
		
		if (ackFlag == RR_FLAG) {
			//RR. Move the window properly.
			updateWindow(windowSize, bottomEdge, upperEdge, ack);
		}
		else if (ackFlag == SREJ_FLAG) {
			//SREJ. Resend the requested packet.
			resendPacket = ack % windowSize;
			send_buf(winBuf[resendPacket].buf, winBuf[resendPacket].buf_len, connection,
				winBuf[resendPacket].flag, winBuf[resendPacket].seqNum, packet);
		}
	}
	return SEND_DATA;

}

//Retrieve the ACK packet from the Server
uint32_t getAck(Connection *connection, uint32_t *ack) {
	uint8_t packet[MAX_LEN] = {0};
	uint8_t flag;
	int32_t seqNum = 0;
	if (recv_buf(packet, 1000, connection->sk_num, connection, &flag, &seqNum) != CRC_ERROR) {
		memcpy(ack, packet, sizeof(uint32_t));
		return flag;
	}
	return CRC_ERROR;
}

//Adjust Window 
void updateWindow (int32_t windowSize, int32_t *bottomEdge, int32_t *upperEdge, uint32_t ackNum) {
	*bottomEdge = ackNum;
	*upperEdge = *bottomEdge + windowSize;
}

//Window is Closed. Resend the Bottom
STATE winClosed (Connection *connection, int32_t *bottomEdge, int32_t *upperEdge, Window *windowBuf, int32_t windowSize, uint32_t *resendCnt) {
	uint8_t packet[MAX_LEN] = {0};
	int32_t resend = *bottomEdge % windowSize;
	uint32_t ackNum, ackFlag;
	int32_t index = 0;

	//Blocking 1 second select 
	if (selectCall(connection->sk_num, 0, 0, 1)) {
      printf("Select true.\n");
      ackFlag = getAck(connection, &ackNum);
		if (ackFlag == END_OF_FILE) {
			//ACK returns EOF
			return END_DATA;
		}
		else if (ackFlag == SREJ_FLAG) {
			//SREJ. Return requested Packet
			index = ackNum % windowSize;
			send_buf(windowBuf[index].buf, windowBuf[index].buf_len, connection, windowBuf[index].flag, windowBuf[index].seqNum, packet);
			*resendCnt = 0;
			return WIN_CLOSED;
		}
		else if (ackFlag == RR_FLAG) {
			//RR. Update Window, then return to SEND_DATA state
			updateWindow(windowSize, bottomEdge, upperEdge, ackNum);
			*resendCnt = 0;
			return SEND_DATA;
		}
		return WIN_CLOSED;
	}
	else {
		//Resend lowest thing in the Window; increment the resend Counter
		send_buf(windowBuf[resend].buf, windowBuf[resend].buf_len, connection, windowBuf[resend].flag, windowBuf[resend].seqNum, packet);
		(*resendCnt)++;
		return WIN_CLOSED;
	}
	return DONE;
}

//Last Packet to be sent from rCopy
STATE lastPacket (Window *windowBuf, int32_t windowSize, Connection *connection, int32_t *bottomEdge, int32_t *upperEdge, uint32_t *lastCnt) {
	int32_t recv_flag, resend;
	uint32_t ack, resendPacket;
	uint8_t packet[MAX_LEN] = {0};

	//1-second Blocking Select
	if (selectCall(connection->sk_num, SHORT_TIME, 0, 1)) {
		recv_flag = getAck(connection, &ack);
		if (recv_flag == END_OF_FILE) {
			//EOF ACK has been received. Terminate Client.
			return DONE;
		}
		else if (recv_flag == SREJ_FLAG) {
			//Server still missing packets. Resend requested packet.
			resendPacket = ack % windowSize;
			send_buf(windowBuf[resendPacket].buf, windowBuf[resendPacket].buf_len, connection,
				windowBuf[resendPacket].flag, windowBuf[resendPacket].seqNum, packet);
			*lastCnt = 0;
		}
		else if (recv_flag == RR_FLAG) {
			//Move the Window.
			updateWindow(windowSize, bottomEdge, upperEdge, ack);
			*lastCnt = 0;
			return END_DATA;
		}
		else {
			return END_DATA;
		}
	}
	//Server didn't get last packet. Resend packet; increment counter.
	resend = *bottomEdge % windowSize;
	send_buf(windowBuf[resend].buf, windowBuf[resend].buf_len, connection, windowBuf[resend].flag, windowBuf[resend].seqNum, packet);
	(*lastCnt)++;
	return END_DATA;
}
