#ifndef _PROXY_H_
#define _PROXY_H_

#include "hcfs.h"
#include "index.h"
#include "buffer.h"
#include "hoc.h"
#include "elevator.h"

#include <sys/epoll.h>
#include <sys/mman.h>
#include <syscall.h>
#include <error.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "http.h"
#include <signal.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "dns.h"
#include "helper.h"
#include "locks.h"
#include "conn.h"
#include "batch.h"
#include "getLocation.h"
#include "unp.h"

#define PRINT_PROXY_DEBUG 100

#define MSL 120
#define MAX_RETRIES 0
#define LARGE_LIMIT 262144
#define CONN_LESS_LIMIT 256 * 1024 / FILE_BLOCK_SIZE
#define HOC 'h'
#define DOB 'd'
#define EXP_CHECK_TIME 30.0
#define ITS_WRITE_TIME 30.0
#define ULTIMATE_WRITE_DEADLINE 15.0
#define INDEX_WRITE_DEADLINE 60.0
#define COME_AGAIN_TIME 100000
#define MAX_BATCH_SIZE 400 * 1024 / FILE_BLOCK_SIZE

typedef struct timeval stamp;

#ifdef PRINT_PROXY_DEBUG
extern int expireCount;
extern int writable;
#endif

#define ERRORLOG 1
#define COMMONLOG 2

extern unsigned long rands[];
extern stamp usageStamps[ FD_SETSIZE ];
extern char *readTran[ FD_SETSIZE ];
extern char *writeTran[ FD_SETSIZE ];
extern int (*readHandlers[ FD_SETSIZE ])(int);
extern int (*writeHandlers[ FD_SETSIZE ])(int);
extern int (*deleteHandlers[ FD_SETSIZE ])(int, stamp); 
extern char* connTran[ FD_SETSIZE ];
extern char* respTran[ FD_SETSIZE ];
extern int *dataHelperWrites;
extern int *dnsHelperWrites;
extern int presentDataHelper;
extern int presentDNSHelper;
extern int numDataHelpers;
extern int numDNSHelpers;
extern char* datHelperStatus;
extern char* dnsHelperStatus;
extern list *dnsList;
extern htab *dnsCache;
extern htab *dTranURL;
extern htab *asyncTab;
extern htab *fTab;
extern htab *rTranTab;
extern htab *mapTab;
extern htab *dnsTranTab;
extern char genBuffer[ 20000 ];
extern int genBufferLen;
extern list *delReqList;
extern list *delResList;
extern list *delConList;
extern list *newConnList;
extern int presentClientCount;
extern int presentReqCount;
extern int presentResCount;
extern int presentConCount;
extern int presentBufCount;
extern int presentConnCount;
extern int presentBatchTotal;
extern int presentCacheCount;
extern int presentBatchCount;
extern int presentIPBatchCount;
extern int presentIPCount;
extern int memLimit;
extern int cLimit;
extern int dLimit;
extern int listenFd;
extern char canConnect;
extern fd_set *readFDSetPtr;
extern fd_set *writeFDSetPtr;
extern list *globalLargeBufferQueue;
extern list *globalSmallBufferQueue;
extern int logHelperWrite;
extern int errorHelperWrite;
extern char *staticGateway;
extern int gatewayPort;
extern int noCache;
extern int* diskLens;
extern int* diskSeeks;
extern int* diskVersions;
extern int* totalBatchOffsets;
extern int presentBatchVersion;
extern list **presentResponseBatch;
extern list** blockLists;
extern list *helperWaitList;
extern char batchWriteLock;
extern int numDisks;
extern char* writeBuffer;
extern int writeBufferSize;
extern int totalMapEntryCount;
extern char versionChange;
extern char diskBatchesReady;
extern int totalBlocksUsed;
extern list* globalWriteWaitList;
extern struct timeval timeSinceProcess;
extern struct timeval timeSinceWrite;
extern char epollDat;
extern char* epollTran[ FD_SETSIZE ];
extern int epfd;
extern int totalCacheCount;
extern elevator** diskSchedulers;

typedef struct mapEntry
{
  list *presentList;
  URLBlock presentBlock;
  char *buffer;
} mapEntry;

typedef struct relOb{
  list *urls;
  int size;
} relOb;
  
int getBestMatch( httpResponse *presentRTranPtr, blockHeader **pHead, char **aData, int *match );
int getEmptyBlock( blockHeader **pHead, int hash, int *match );
int sendData( httpResponse *presentRTranPtr );
int sendHeader( httpResponse *presentRTranPtr );
int writeBlockHeader( httpResponse *presentRTranPtr, blockHeader *head );
int writeHeader( httpResponse *presentRTranPtr, char *data );
int writeData( httpResponse *presentRTranPtr, char *data, int len );
int delResponseTranLazy( httpResponse *res );
int delRequestTranLazy( httpRequest *req );
int delConnectionLazy( connection *conn );
int delResponseTranPerm( httpResponse *res );
int delRequestTranPerm( httpRequest *req );
int delConnectionPerm( connection *conn );
int updateUsageStamp( int fd );
int formSendHeader( httpResponse *presentRTranPtr );
int formWriteHeader( httpResponse *presentRTranPtr );
int hdrCpy( char *dest, char *src );
int sendRequestHeader( httpResponse *presentRTranPtr );
int sendRequestData( int fd, httpRequest *presentQTranPtr );

int readyForDownload( httpResponse *presentRTranPtr );
int readyForPut( httpResponse *presentRTranPtr );
int sendNegReply( httpResponse *presentRTranPtr );
int searchAndConnect( httpResponse *presentRTranPtr );
int initConnection( connection *presentConn, httpResponse *presentRTranPtr );
int checkDNSCacheAndConnect( connection *newConn );
int connectServerLazy( connection *presentConn );
int connectServerPerm( connection *presentConn );
int connectAllPendingServers();
int deletePerms();
int sendRequest( httpResponse *presentResPtr );

int checkClientExpiry( int fd, stamp now );
int checkServerAvailability( int fd, stamp now );
int checkPersistentConnection( int fd, stamp now );

int handleDownloadRead( int fd );


int joinFlow( httpRequest *presentQTrantr );
int scheduleResolve( connection *presentConn );

int checkForFreeBlocks( httpResponse *presentRTranPtr );
int checkForWrite( httpResponse *presentRTranPtr );
int getFreeLocation( setHeader *presentHeader, unsigned int hash );
int getBestMatchByIndex( unsigned int hash, setHeader *presentHeader );
int prepareWrite( char *buffer, httpResponse *presentRTranPtr );
int cacheHeaders( unsigned int hash, blockHeader **presentHeaders );

int logNow();
int logResponse( httpResponse *presentResponse, struct timeval *now );
char *getMethod( int method );
char *getErrorMessage( int logType );
int whichDatHelper();
int whichDNSHelper();

int persistentClient( httpRequest *client );
int persistentConn( connection *conn );
int manageConnection( connection *conn );

float timeVal( struct timeval *timenow );
float timeGap( struct timeval *now, struct timeval *then );
inline int determineSmartLength( int, int, int );
int processFurtherBlocks( httpResponse *response );
int processBatch( batch* );
int checkBatchWritesNow();
int formDiskBatches();
int addToRelOb( relOb*, httpResponse* );
void printRequestCount();

int resumeDownload( int fd );
int handleClientRead( int fd );
int resumePosting( int fd );
inline void printStats();

#endif
