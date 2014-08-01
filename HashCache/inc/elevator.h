#ifndef _ELEVATOR_H_
#define _ELEVATOR_H_

#include "list.h"
#include "helper.h"
#include "hcfs.h"
#include <time.h>

#define NUMBER_CUTOFF 15
#define TIME_CUTOFF 0.1

typedef struct elevator elevator;
typedef struct eleRequest eleRequest;

elevator* createElevator( int helper, int bufferSize );
eleRequest* createEleRequest( char* buffer );
void deleteEleRequest( eleRequest* oldEl );
void destroyElevator( elevator* );
int addRequest( elevator *pEl, diskRequest* newRequest );
int scheduleNextRequest();
list* getMergedRequests( eleRequest* pRequest );
diskRequest* getMergedRequest( eleRequest* pRequest );
void* boundaryMalloc( size_t x );
void printEleStats( elevator* );
float timeGap( struct timeval*, struct timeval* );
void returnElevator( elevator* x );

#endif
