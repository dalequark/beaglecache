#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "applib.h"
#include "util.h"

HANDLE hdebugLog = NULL;
char* test_msg = "hello world!";
#define SERVER_PORT	(8864)

void ping(char* serverName, short serverPort)
{
	in_addr_t netAddr;
	bzero(&netAddr, sizeof(in_addr_t));
	int fd = MakeConnection(serverName, netAddr, serverPort, FALSE);
	if (fd == -1) {
		printf("connection to %s:%d failed\n", serverName, serverPort);
		return;
	}

	char recv_buf[1024];
	struct timeval start, end;
	UpdateCurrentTime(&start);
	int numSend = send(fd, test_msg, strlen(test_msg), 0);
	int numRcvd = recv(fd, recv_buf, sizeof(recv_buf), 0);
	UpdateCurrentTime(&end);
	printf("%d sent, %d recv, %d msec\n", numSend, numRcvd,
			GetTimeGapMS(&start, &end));
	close(fd);
}

void pong(short serverPort)
{
	int server_fd = CreatePrivateAcceptSocket(serverPort, FALSE);
	if (server_fd == -1) {
		printf("server socket creation failed\n");
		return;
	}

	while (TRUE) {
		struct sockaddr sa;
		socklen_t len = sizeof(struct sockaddr);
		int fd = accept(server_fd, &sa, &len);
		if (fd == -1) {
			printf("accept failed\n");
			return;
		}

		char recv_buf[1024];
		struct timeval start, end;
		UpdateCurrentTime(&start);
		int numRcvd = recv(fd, recv_buf, sizeof(recv_buf), 0);
		int numSend = send(fd, test_msg, strlen(test_msg), 0);
		UpdateCurrentTime(&end);
		printf("%d sent, %d recv, %d msec\n", numSend, numRcvd,
				GetTimeGapMS(&start, &end));
		close(fd);
	}
}

int main(int argc, char** argv)
{
	if (argc == 1) {
		pong(SERVER_PORT);
	}
	if (argc == 2) {
		ping(argv[1], SERVER_PORT);
	}

	return 0;
}
