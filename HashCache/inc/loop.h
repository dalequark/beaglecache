#ifndef _LOOP_H_
#define _LOOP_H_

#include "init.h"
#include "index.h"
#include "http.h"
#include "fdset.h"
#include "proxy.h"
#include "queue.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <syscall.h>
#include <error.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <limits.h>

#define PRINT_LOOP_DEBUG 100
#define PRINT_TIME_DEBUG 100

extern int epfd;
extern struct epoll_event *epollEventsIn;
extern struct epoll_event *epollEventsOut;
extern int numHelpers;
extern int port;
extern int *dataHelperReads;
extern int *dnsHelperWrites;
extern int firstFd;
extern int secondFd;
extern int listenFd;
extern htab* dTranURL;
extern htab* rTranTab;
extern htab* asyncTab;
extern htab* connTab;
extern htab* fTab;
extern htab* dnsTranTab;
extern char* readTran[ FD_SETSIZE ];
extern char* writeTran[ FD_SETSIZE ];
extern stamp usageStamps[ FD_SETSIZE ];
extern char* connTran[ FD_SETSIZE ];
extern int (*readHandlers[ FD_SETSIZE ])(int);
extern int (*writeHandlers[ FD_SETSIZE ])(int);
extern int (*deleteHandlers[ FD_SETSIZE ])(int,stamp); 
extern int timerHelperRead;
extern int timerHelperWrite;
extern int indexHelperWrite;
extern int errorHelperWrite;
extern int presentMapNum;
extern int presentClientCount;
extern int presentConnCount;
extern int *mapSize;
extern char **mapData;
extern int memLimit;
extern int cLimit;
extern int dLimit;
extern int indexSize;
extern int cloneBufferSize;
extern int noCache;
extern unsigned long *subnetMasks;
extern unsigned long *subnetMatches;
extern int subnetCount;
extern elevator** diskSchedulers;

struct setHeader;

int loop();
int makeFDSets();

int handleClientRead( int fd );
int handleDownloadRead( int downFd );
int handleConnect( int fd );
int resumeDownload( int fd );
int resumeDiskRead( int fd );
int resumePosting( int fd );

int checkClientAddress( struct sockaddr_in *cliAddr );
int checkFollow( httpResponse *presentRTranPtr );
int sendData( httpResponse *presentRTranPtr );

int readn( int fd, void *data, int length );
int writen( int fd, char *data, int length );

int processFurtherBlocks( httpResponse *response );
int schedulePendingDiskRequests();
void printRequestCount();
#endif
