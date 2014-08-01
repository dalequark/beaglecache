#ifndef _BATCH_H_
#define _BATCH_H_

#include "hashtab.h"
#include "list.h"
#include "memSys.h"
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef int batchKey;
typedef struct batch batch;
typedef struct batchElement batchElement;

#define BATCH_DONE 1
#define BATCH_NOT_DONE 0
#define MAX_LENGTH 100000
#define BATCH_WRITE_DEADLINE 30
#define BATCH_TIME_STEP 1.0

batch* formNewBatch( char *ip, batchElement *eThis );
batch* addToBatch( char *ip, void* dat );
int initBatchSystem();
batch* getNextEligibleBatch( int );
int startBatchProcess( int* );
list* getResponseList( batch* thisBatch );
int* getIP( batch* thisBatch );
int printBatchStats();
int addWithPriority( list *urls, void* dat, int contentType, int contentLength );
int beginAllocCycle();
void* getResponse( batchElement* );
int processTime();
int addBack( batch* );
batch* getLocalhostBatch();

#endif
