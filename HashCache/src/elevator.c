#include "elevator.h"

struct elevator
{
  list* currentList;
  list* sortedList;
  char scheduled;
  int helper;
  char* buffer;
  int bufferSize;
  struct timeval lastSort;
};

struct eleRequest
{
  list* requests;
  diskRequest mergedRequest;
  char* buffer;
  char* actual;
  int curEnd;
};

extern int numDisks;

elevator* createElevator( int helper, int bufferSize )
{
  elevator* newEl = malloc( sizeof( elevator ) );
  newEl->currentList = create();
  newEl->sortedList = create();
  newEl->scheduled = FALSE;
  newEl->helper = helper;
  newEl->buffer = boundaryMalloc( bufferSize );
  newEl->bufferSize = bufferSize;
  gettimeofday( &newEl->lastSort, NULL );
  return newEl;
}

eleRequest* createEleRequest( char* buffer )
{
  eleRequest* pRequest = malloc( sizeof( eleRequest ) );
  pRequest->requests = create();
  pRequest->buffer = buffer;
  pRequest->curEnd = 0;
  return pRequest;
}

void destroyElevator( elevator* oldEl )
{
  ldestroy( oldEl->currentList );
  ldestroy( oldEl->sortedList );
  free( oldEl );
}

void deleteEleRequest( eleRequest* oldEl )
{
  startIter( oldEl->requests );
  //fprintf( stderr, "Deleting %d disk requests\n", oldEl->requests->size );
  char* data;

  while( notEnd( oldEl->requests ) == TRUE )
    {
      data = getPresentData( oldEl->requests );
      //fprintf( stderr, "free %d\n\n", (int)data );
      free( data );
      deletePresent( oldEl->requests );
    }
  
  ldestroy( oldEl->requests );
  free( oldEl );
}

int addRequest( elevator *pEl, diskRequest* newRequest )
{
  if( pEl == NULL || newRequest == NULL )
    return -1;

  addToTail( pEl->currentList, newRequest->remOffset, newRequest );

  scheduleNextRequest( pEl );
  return 0;
}

static char checkContinuity( elevator* pEl, eleRequest* eRequest, diskRequest* pRequest )
{
  //  fprintf( stderr, "Remoffset %d\n", pRequest->remOffset );

  if( getSize( eRequest->requests ) == 0 )
    {
      //addToTail( eRequest->requests, 0, pRequest );
      memcpy( &eRequest->mergedRequest, pRequest, sizeof( diskRequest ) );
      eRequest->mergedRequest.entry = (char*)eRequest;
      eRequest->curEnd = pRequest->remOffset + numDisks * ( pRequest->writeLen / FILE_BLOCK_SIZE );
      eRequest->mergedRequest.writeLen = 0;

      if( pRequest->type == WRITE )
	{
	  eRequest->mergedRequest.buffer = pRequest->buffer;
	  return WRITE;
	}
      else
	{
	  eRequest->mergedRequest.buffer = eRequest->buffer;
	  return READ_MERGE;
	}
    }
  else
    {
      if( pRequest->type == WRITE )
	{
	  return WRITE;
	}

      if( eRequest->mergedRequest.writeLen + pRequest->writeLen > pEl->bufferSize )
	{
	  return READ;
	}

      if( eRequest->curEnd + numDisks == pRequest->remOffset )
	{
	  return READ_MERGE;
	}
      else
	{
	  return READ;
	}
    }

  return READ;
}

static int mergeRequest( eleRequest* eRequest, diskRequest* pRequest )
{
  eRequest->mergedRequest.writeLen += pRequest->writeLen;
  addToTail( eRequest->requests, 0, (char*)pRequest );
  return 0;
}

static void formSchedule( elevator *pEl )
{
  diskRequest* pRequest;
  eleRequest* eRequest = NULL;
  list* temp = create();

  startIter( pEl->currentList );

  while( notEnd( pEl->currentList ) == TRUE )
    {
      pRequest = (diskRequest*)getPresentData( pEl->currentList );
      addToHead( pEl->sortedList, pRequest->remOffset, (char*)pRequest );
      deletePresent( pEl->currentList );
    }

  //  fprintf( stderr, "Before: %d\n", getSize( pEl->sortedList ) );
  sort( pEl->sortedList );
  //fprintf( stderr, "After: %d\n", getSize( pEl->sortedList ) );

  startIter( pEl->sortedList );

  while( notEnd( pEl->sortedList ) == TRUE )
    {
      eRequest = createEleRequest( pEl->buffer );

      while( notEnd( pEl->sortedList ) == TRUE )
	{
	  pRequest = (diskRequest*)getPresentData( pEl->sortedList );

	  if( checkContinuity( pEl, eRequest, pRequest ) == READ_MERGE )
	    {
	      /*      if( eRequest->requests->size > 1 )
		fprintf( stderr, "Merge - %c\n", pRequest->type );
	      else
	      fprintf( stderr, "First - %c\n", pRequest->type );*/

	      mergeRequest( eRequest, pRequest );
	      deletePresent( pEl->sortedList );
	    }
	  else
	    {
	      //	      fprintf( stderr, "No Merge - %c\n", pRequest->type );
	      if( getSize( eRequest->requests ) == 0 )
		{
		  mergeRequest( eRequest, pRequest );
		  deletePresent( pEl->sortedList );
		}

	      break;
	    }
	}

      addToTail( temp, 0, (char*)eRequest );
    }
  
  ldestroy( pEl->sortedList );
  pEl->sortedList = temp;

  return;
}

int scheduleNextRequest( elevator *pEl )
{
  eleRequest* thisRequest;
  struct timeval now;
  gettimeofday( &now, NULL );
  float gap;
  int status;

  if( getSize( pEl->sortedList ) == 0 )
    {
      gap = timeGap( &now, &pEl->lastSort );

      if( getSize( pEl->currentList ) < NUMBER_CUTOFF && gap < TIME_CUTOFF )
	return 0;

      //  fprintf( stderr, "Gap: %f Size: %d\n", gap, getSize( pEl->currentList ) );
      memcpy( &pEl->lastSort, &now, sizeof( struct timeval ) );
      formSchedule( pEl );

      if( getSize( pEl->sortedList ) == 0 )
	{
	  return 0;
	}
    }

  if( pEl->scheduled == FALSE )
    {
      pEl->scheduled = TRUE;
      thisRequest = (eleRequest*)getHeadData( pEl->sortedList );
      deleteHead( pEl->sortedList );
      status = write( pEl->helper, (char*)(&thisRequest->mergedRequest), 
		      sizeof( diskRequest ) );
      //fprintf( stderr, "Buffer: %u\n", (unsigned)thisRequest->mergedRequest.buffer );
    }

  return 0;
}

list* getMergedRequests( eleRequest* pRequest )
{
  return pRequest->requests;
}

diskRequest* getMergedRequest( eleRequest* pRequest )
{
  return &(pRequest->mergedRequest);
}

void* boundaryMalloc( size_t x )
{
  char *actual = malloc( x + 4096 );
  unsigned aligned;
  
  aligned = ((size_t)actual / 4096)*4096;

  if( aligned < (size_t)actual )
    aligned += 4096;

  //fprintf( stderr, "Buffer: %u\n", aligned );
  return (char*)aligned;
}

void printEleStats( elevator* pEl )
{
  fprintf( stderr, "Current: %d Sorted: %d\n", getSize( pEl->currentList ), getSize( pEl->sortedList ) );
}

void returnElevator( elevator* pEl )
{
  pEl->scheduled = FALSE;
}
