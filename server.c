#include "networks.h"
#include "cpe464.h"

/* Enum Declaration for State Differentiation */
typedef enum State STATE;
enum State {
	START, FILENAME, DONE, READ_DATA, DATA_RCV
};

//Function Headers 
int processArgs (int argc, char *argv[]);
void processServer(int serverSkNum);
void processClient(int32_t serverSkNum, uint8_t *buf, int32_t recvLen, Connection *client);
STATE fileName (Connection *client, uint8_t *buf, int32_t recvLen, int32_t *dataFile, int32_t *bufSize, int32_t *windowSize);
STATE getData(Connection *connection, Window *winBuf, int32_t dataFile, int32_t bufSize,
	int32_t windowSize, int32_t *expectedSeqNum, uint32_t *serverSeqNum, uint32_t *bufferedDataSize);
void sendAck(Connection *connection, uint8_t flagType, int32_t recvSeqNum, uint32_t *seqNum);
STATE recoverData(Connection *connection, Window *winBuf, int32_t dataFile, int32_t bufSize,
	int32_t windowSize, int32_t *expectedSeqNum, uint32_t *serverSeqNum, uint32_t *bufferedDataSize);
STATE checkBuffer (Connection *connection, Window *winBuf, int32_t dataFile, int32_t bufSize,
	int32_t windowSize, int32_t recvSeqNum, int32_t *expectedSeqNum, uint32_t *serverSeqNum, uint32_t *bufferedDataSize);


int main(int argc, char *argv[]) {
	int32_t serverSkNum = 0;
	int portNum = 0;

	portNum = processArgs(argc, argv); //Check arguments are valid

	/*Initialize the Error functions */
	sendtoErr_init(atof(argv[1]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

	serverSkNum = udpSetup(portNum);

	processServer(serverSkNum);

	return 0;
}

//Check Arguments for Validity
int processArgs (int argc, char *argv[]) {
	int portNumber = 0;
	if (argc < 2 || argc > 3) {
		printf("Usage: %s error_rate <Port Number>\n", argv[0]);
		exit(-1);
	}
	if (atof(argv[1]) < MIN_ERR || atof(argv[1]) > MAX_ERR) {
		printf("Invalid error Rate. (Must be between 0 and 1) Input Error: %f\n", atof(argv[1]));
		exit(-1);
	}
	if (argc == 3) {
		portNumber = atoi(argv[2]);
	}
	return portNumber;
}

//Run the Server
void processServer(int serverSkNum) {
	pid_t pid = 0;
	int status = 0;
	uint8_t buf[MAX_LEN];
	Connection client;
	uint8_t flag = 0;
	int32_t seqNum = 0, recvLen = 0;

	while (1) { //Loop until force closed
		if (selectCall(serverSkNum, SHORT_TIME, 0, NOT_NULL) == 1) {
			//Someone is connecting
			recvLen = recv_buf(buf, MAX_LEN, serverSkNum, &client, &flag, &seqNum);
			if (recvLen != CRC_ERROR) {
				if ((pid = fork()) < 0) {
					perror("fork");
					exit(-1);
				}
				if (pid == 0) {
					//New Client. Process.
					processClient(serverSkNum, buf, recvLen, &client);
					exit(0);
				}
			}
			while (waitpid(-1, &status, WNOHANG) > 0) {}
		}
	}
}

//Process the Client
void processClient(int32_t serverSkNum, uint8_t *buf, int32_t recvLen, Connection *client) {
	STATE state = START;
	int32_t dataFile = 0;
	int32_t bufSize = 0;
	int32_t windowSize = 0;
	uint32_t bufferedData = 0;
	int32_t seqNum = START_SEQ_NUM;
	uint32_t serverSeqNum = 1;
	Window *winBuf;

	//Loops until Client is Done, or disappears. 
	while (state != DONE) {
		switch (state) {
			case START:
				//Nothing here. Move on.
				state = FILENAME;
				break;
			case FILENAME:
				//Get the filename info from client, open and prep for writing
				//Initialize the buffer to store unexpected packets
				state = fileName(client, buf, recvLen, &dataFile, &bufSize, &windowSize);
				winBuf = calloc(windowSize, sizeof(Window));
				break;
			case READ_DATA:
				//Receive data from Client and process it
				state = getData(client, winBuf, dataFile, bufSize, windowSize, &seqNum, &serverSeqNum, &bufferedData);
				break;
			case DATA_RCV:
				//Data was lost. Recover it.
				state = recoverData(client, winBuf, dataFile, bufSize, windowSize, &seqNum, &serverSeqNum, &bufferedData);
				break;
			case DONE: 
				//Client is done. 
				break;
			default:
				//Should never get in here.
				printf("Error: In Default Case.\n");
				state = DONE;
				break;
		}
	}

}

//Gets filename info from Client, Opens/Creates file w/ proper permissions
STATE fileName (Connection *client, uint8_t *buf, int32_t recvLen, int32_t *dataFile, int32_t *bufSize,int32_t *windowSize) {
	uint8_t response[1];
	char filename[MAX_LEN];
	STATE returnValue = DONE;
	memcpy(bufSize, buf, SIZE_OF_BUF_SIZE);
	memcpy(windowSize, buf + 4, 4);
	memcpy(filename, &buf[8], recvLen -8);

	/*Create client socket to allow for processing this particular client */
	if ((client->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror ("filename, open client socket");
		exit(-1);
	}

	if (((*dataFile) = open(filename, O_CREAT | O_TRUNC |O_WRONLY, 0666)) < 0) {
		//File unable to be opened/created. BAD_FILE returned.
		send_buf(response, 0, client, FN_BAD, 0, buf);
		returnValue = DONE;
	}
	else {
		//File successfullly opened/created. GOOD_FILE returned.
		send_buf(response, 0, client, FN_GOOD, 0, buf);
		returnValue = READ_DATA;
	}

	return returnValue;

}

//Receive data from Client and process 
STATE getData(Connection *connection, Window *winBuf, int32_t dataFile, int32_t bufSize,
	int32_t windowSize, int32_t *expectedSeqNum, uint32_t *serverSeqNum, uint32_t *bufferedDataSize) {
	int32_t recvSeqNum = 0, data_len = 0;
   uint8_t flag = 0, data_buf[MAX_LEN];
   int32_t index = 0;
   
   /* If server receives nothing for 10 seconds close connection */
   if (!selectCall(connection->sk_num, LONG_TIME, 0, 1)){
      return DONE;
   }
   

   data_len = recv_buf(data_buf, bufSize + 8, connection->sk_num, connection, &flag, &recvSeqNum);
   if (data_len == CRC_ERROR) {
   	//Bits fliped
      return READ_DATA;
   }
   if (recvSeqNum == *expectedSeqNum) {
   	//Data was what was expected. Write to file. 
   	write(dataFile, &data_buf, data_len);
   	
      
		(*expectedSeqNum)++;
   }
   else if (recvSeqNum > *expectedSeqNum) {
   	//Unexpected Data. Store in Buffer and send SREJ. Enter Data Recovery
   	index = recvSeqNum % windowSize;
   	memcpy(winBuf[index].buf, data_buf, data_len);
   	winBuf[index].seqNum = recvSeqNum;
   	winBuf[index].buf_len = data_len;
   	winBuf[index].flag = flag;
   	(*bufferedDataSize)++;

   	sendAck(connection, SREJ_FLAG, *expectedSeqNum, serverSeqNum);
   	return DATA_RCV;
   }
   else {
   	//Send RR.
   	sendAck(connection, RR_FLAG, recvSeqNum, serverSeqNum);
   	return READ_DATA;
   }

	if (flag == DATA_FLAG) {
		sendAck(connection, RR_FLAG, recvSeqNum, serverSeqNum);
		(*expectedSeqNum)++;
   }
   else if (flag == END_OF_FILE) {
   	//Send EOF acknowledgement. Close connection after.
      sendAck(connection, END_OF_FILE, recvSeqNum, serverSeqNum);
      return DONE;
   }
   return READ_DATA;
}

//Sends ACK packets to client
void sendAck(Connection *connection, uint8_t flagType, int32_t recvSeqNum, uint32_t *seqNum) {
	uint8_t data[MAX_LEN], packet[MAX_LEN];
	if (flagType == RR_FLAG || flagType == END_OF_FILE) {
		recvSeqNum++;
	}
	*seqNum = recvSeqNum;
	if (flagType != SREJ_FLAG){
		(*seqNum)++;
	}
	//Set sequence number information into buf
	memcpy(&data[0], &recvSeqNum, 4);

	//Send it on its merry way.
	send_buf(data, sizeof(int32_t), connection, flagType, *seqNum, packet);
	
}

//Something Wrong. Data Recovery State.
STATE recoverData(Connection *connection, Window *winBuf, int32_t dataFile, int32_t bufSize,
	int32_t windowSize, int32_t *expectedSeqNum, uint32_t *serverSeqNum, uint32_t *bufferedDataSize) {
	int32_t recvSeqNum = 0, data_len = 0;
	int32_t index = 0;
	uint8_t data_buf[MAX_LEN];
	uint8_t flag;

	//Terminates Connection if client is quiet for over 10 secs
	if (!selectCall(connection->sk_num, LONG_TIME, 0, 1)){
      return DONE;
   }
   
   //Get Data from the client
   data_len = recv_buf(data_buf, bufSize + 8, connection->sk_num, connection, &flag, &recvSeqNum);
   if (data_len == CRC_ERROR) {
   	//Bit Flipped
   	return DATA_RCV;
   }
   if (recvSeqNum == *expectedSeqNum) {
   	//Resent packet was what was expected. Write to file.
   	write(dataFile, &data_buf, data_len);
   	(*expectedSeqNum)++;

   	//Move things from buffer to file.
   	return checkBuffer(connection, winBuf, dataFile, bufSize, windowSize, recvSeqNum, expectedSeqNum, serverSeqNum, bufferedDataSize);
   }
   else if (recvSeqNum > *expectedSeqNum) {
   	//Resent Packet is not what was expected; buffer and SREJ 
   	index = recvSeqNum % windowSize;
   	memcpy(winBuf[index].buf, data_buf, data_len);
   	winBuf[index].seqNum = recvSeqNum;
   	winBuf[index].buf_len = data_len;
   	winBuf[index].flag = flag;
   	(*bufferedDataSize)++;
   	sendAck(connection, SREJ_FLAG, *expectedSeqNum, serverSeqNum);
   	return DATA_RCV;
   }
   else {
   	//Resent Packet is a lower seqNum than what we want.
   	//Do nothing w/ the data and SREJ for the original packet.
   	sendAck(connection, SREJ_FLAG, *expectedSeqNum, serverSeqNum);
   	return DATA_RCV;
   }
}

//Processes the Buffer and moves everything to File if possible
STATE checkBuffer (Connection *connection, Window *winBuf, int32_t dataFile, int32_t bufSize,
	int32_t windowSize, int32_t recvSeqNum, int32_t *expectedSeqNum, uint32_t *serverSeqNum, uint32_t *bufferedDataSize) {
	int32_t index = 0;
	while (*bufferedDataSize > 0) {
		//Loops while the buffer isn't empty
		index = *expectedSeqNum % windowSize;
		if (*expectedSeqNum == winBuf[index].seqNum) {
			//What is in the buffer is what we want; Write to file.
			write(dataFile, winBuf[index].buf, winBuf[index].buf_len);
			//Update the expected sequence number
			(*expectedSeqNum)++;
			//Buffer now "has" one less thing. lower bufferedDataSize 
			(*bufferedDataSize)--;
		}
		else {
			//Hole in the buffer; SREJ for that packet
			sendAck(connection, SREJ_FLAG, *expectedSeqNum, serverSeqNum);
			return DATA_RCV;
		}
	}
	if (winBuf[index].flag == END_OF_FILE) {
		//Buffer Empty; The packet in the buffer was the last from the Client.
		//Send ACK for it, then close connection
		sendAck(connection, END_OF_FILE, recvSeqNum, serverSeqNum);
		return DONE;
	}
	else {
		//Buffer Empty; Send RR for the next packet.
		//Return to READ_DATA state
		sendAck(connection, RR_FLAG, recvSeqNum, serverSeqNum);
		return READ_DATA;
	}

}


