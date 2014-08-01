#include "batch.h"

static htab *currentBatches;
static int numBatches;
static list** timeLists;
static int currentTime;
static unsigned localIP;

#define BATCH_TIME_CUT_OFF 2000.0

struct batch {
  list *urls;
  struct timeval start;
  void* listPos;
  char inList;
  unsigned ip;
};

struct batchElement {
  void* dat;
  int addedTime;
};

int initBatchSystem()
{
  int i;
  currentBatches = hcreate( 10 );
  numBatches = 0;
  currentTime = 0;
  struct in_addr local;
  inet_aton( "127.0.0.1", &local );
  localIP = local.s_addr;
  timeLists = malloc( sizeof( list* ) * BATCH_WRITE_DEADLINE );

  for( i = 0; i < BATCH_WRITE_DEADLINE; i++ )
    {
      timeLists[ i ] = create();
    }

  return 0;
}

batch* formNewBatch( char *ip, batchElement *newElement )
{
  batch *newBatch ;
#ifdef USE_MEM_SYS
  newBatch = (batch*)getChunk( thisMemSys );
#else
  newBatch = (batch*)malloc( sizeof( batch ) );
#endif
  newBatch->urls = create();
  newBatch->ip = *(unsigned*)ip;
  addToTail( newBatch->urls, 0, newElement );
  newBatch->inList = TRUE;
  hadd( currentBatches, (char*)&newBatch->ip, sizeof( batchKey ), (void*)newBatch );

  if( memcmp( &newBatch->ip, &localIP, sizeof( int ) ) != 0 )
    addToTail( timeLists[ currentTime ], 0, (void*)newBatch );

  numBatches++;

  return newBatch;
}

batch* addToBatch( char *ip, void *dat )
{
  //  return NULL;
  batch *newBatch;
  batchElement *newElement = malloc( sizeof( batchElement ) );
  newElement->dat = dat;
  newElement->addedTime = currentTime;

  if( hfind( currentBatches, ip, sizeof( batchKey ) ) == TRUE )
    {
      newBatch = (batch*)hstuff( currentBatches );
      addToTail( newBatch->urls, 0, newElement );
      
      if( newBatch->inList == FALSE && 
	  memcmp( &newBatch->ip, &localIP, sizeof( int ) ) != 0 )
	{
	  addToTail( timeLists[ currentTime ], 0, newBatch );
	}

      return newBatch;
    }
  else
    {
      newBatch = formNewBatch( ip, newElement );
      return newBatch;
    }

  return NULL;
}

int delBatch( batch* thisBatch )
{
  ldestroy( thisBatch->urls );
#ifdef USE_MEM_SYS
  delChunk( thisMemSys, thisBatch );
#endif
  return 0;
}

int startBatchProcess( int *oldest )
{
  *oldest = currentTime + 1;
  
  if( *oldest >= BATCH_WRITE_DEADLINE )
    {
      *oldest = 0;
    }

  startIter( timeLists[ *oldest ] );
  return 0;
}

list* getResponseList( batch* thisBatch )
{
  return thisBatch->urls;
}

void* getResponse( batchElement *newElement )
{
  return newElement->dat;
}

int addBack( batch* oldBatch )
{
  batchElement* el = getHeadData( oldBatch->urls );
  
  if( el != NULL && memcmp( &oldBatch->ip, &localIP, sizeof( int ) ) != 0 )
    {
      addToHead( timeLists[ el->addedTime ], 0, oldBatch );
      oldBatch->inList = TRUE;
    }

  return 0;
}

int* getIP( batch* thisBatch )
{
  return (int*)&thisBatch->ip;
}

batch* getNextEligibleBatch( int oldest )
{
  batch* oldBatch;

  if( notEnd( timeLists[ oldest ] ) == TRUE )
    {
      oldBatch = (batch*)getPresentData( timeLists[ oldest ] );
      oldBatch->inList = FALSE;
      deletePresent( timeLists[ oldest ] );
      return oldBatch;
    }
  
  return NULL;
}
	
batch* getLocalhostBatch()
{
  if( hfind( currentBatches, &localIP, sizeof( batchKey ) ) == TRUE ) {
    return hstuff( currentBatches );
  }

  return NULL;
}

int printBatchStats()
{
  fprintf( stderr, "Num batches: %d\n", numBatches );
  return 0;
}

int addWithPriority( list *urls, void *dat, int contentType, int contentLength )
{
  if( contentType == 0 )
    {
      addToHead( urls, 0, dat );
    }
  else
    {
      if( contentLength > 0 )
	addSorted( urls, dat, contentLength );
      else
	addSorted( urls, dat, MAX_LENGTH );
    }

  return 0;
}

int processTime()
{
  if( ++currentTime >= BATCH_WRITE_DEADLINE )
    currentTime = 0;

  return 0;
}
