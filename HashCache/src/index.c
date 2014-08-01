#include "index.h"
#include <stdio.h>

#if COMPLETE_LOG > 0 || SETMEM > 0
extern char* indexFilePath;
static int numSetsUsed;
static int* setUsage;
static int numDeletes;
static unsigned long long clockMasks[ WAYS ];
static unsigned clockShifts[ WAYS ];

static inline unsigned int getInteger( unsigned char *location, int where );
static inline void setInteger( unsigned char *location, int where, unsigned int value );
static inline void decInteger( unsigned char *location, int where );
static inline void incInteger( unsigned char *location, int where );

#ifdef COMPLETE_LOG
static inline void setRemOffset( setHeader *presentSetHeader, int match, int remOffset );
static inline void setRemVersion( setHeader *presentSetHeader, int match, int version );
static inline void setRemLen( setHeader *presentSetHeader, int match, int remLen );
#endif

static inline void setMatch( setHeader *presentSetHeader, int match, 
			     indexHeader presentHeader );

#if LRU > 0
static unsigned int numSets;
static htab *setTab;
static list *setList;

struct setHeader {
  int setNum;
  char headers[ WAYS ];
  char clocks[ WAYS ];
};

static setHeader *headers;
#endif

#if FULL > 0
unsigned int numSets;

struct setHeader {
  //int setNum;
  hbits headers[ WAYS ];
  unsigned char clocks[ (int)(LWAYS * WAYS / (8 * sizeof( char ) )) ];
  //unsigned char clocks[ 4 ];

#ifdef COMPLETE_LOG
  int logOffset[ WAYS ];
  char logVersion[ WAYS ];
  char logLen[ WAYS ];
#endif
};

int onDiskIndexSize = 2 * sizeof( int );

static setHeader *headers;
#endif

/*#if PARTIAL > 0
unsigned int numSets;
int limit;
int numParts;
static htab* setCache;

struct shadowHeader {
  int setNum;
  char headers[ WAYS ];
  char clocks[ WAYS ];
#ifdef COMPLETE_LOG
  int logOffset[ WAYS ];
  char logVersion[ WAYS ];
  char logLen[ WAYS ];
#endif
};

struct setHeader {
  int setNum;
  char headers[ WAYS ];
  char clocks[ WAYS ];
#ifdef COMPLETE_LOG
  int logOffset[ PWAYS ];
  char logVersion[ PWAYS ];
  char logLen[ PWAYS ];
#endif
};

#ifdef COMPLETE_LOG
int onDiskIndexSize = 2 * sizeof( int );
#endif

static setHeader *headers;
#endif
#endif*/
static int diskIndexSize;
static off_t presentIndexOff;
#define INDEX_CHUNK_SIZE 1024 * 1024

int initIndex( unsigned int indexSize, int mode )
{
  int i, j;
  
  clockShifts[ 0 ] = ( WAYS - 1 ) * LWAYS;
  clockMasks[ 0 ] = ((unsigned long long)(WAYS - 1)) << clockShifts[ 0 ];

  for( i = 1; i < WAYS; i++ ) {
    clockShifts[ i ] = clockShifts[ i - 1 ] - LWAYS;
    clockMasks[ i ] = clockMasks[ i - 1 ] >> LWAYS;
  }

  /*  for( i = 0; i < WAYS; i++ ) {
    fprintf( stderr, "%llu %d\n", clockMasks[ i ], clockShifts[ i ] );
    }*/


#if LRU > 0
  numSets = ( indexSize * 1024 * 1024 ) / ( sizeof( setHeader ) + sizeof( element ) + sizeof( hitem ) );
  setTab = hcreate( 10 );
  setList = create();
  headers = (setHeader*)calloc( numSets, sizeof( setHeader ) );
  
  for( i = 0; i < numSets; i++ )
    {
      for( j = 0; j < WAYS; j++ )
	{
	  setInteger( headers[ i ].clocks, j,  j + 1 );
	  headers[ i ].headers[ j ] = -1;
	}
    }

  for( i = 0; i < numSets; i++ )
    {
      switch( mode )
	{
	case REUSEV:
	  headers[ i ].setNum = -1;
	  break;
	default:
	  headers[ i ].setNum = i;
	  addToHead( setList, 0, (char*)(&headers[ i ]) );
	  startIter( setList );
	  hadd( setTab, (char*)(&(headers[ i ].setNum)), sizeof( int ), getIter( setList ) );
	  break;
	}
    }

#endif

  /*#if ZIPF > 0
  numSets = indexSize * 1024 * 1024 / ( sizeof( setHeader ) + sizeof( element ) + sizeof( hitem ) );
  setTab = hcreate( 10 );
  
  for( i = 0; i < NUM_BINS; i++ )
    {
      setLists[ i ] = create();
    }

  headers = (setHeader*)calloc( numSets, sizeof( setHeader ) );

  if( headers != NULL )
    for( i = 0; i < numSets; i++ )
      {
	addToTail( setLists[ 0 ], 0, (char*)(&headers[ i ]) );
      }
  else
    return -1;
    #endif*/

#if  FULL > 0
  fprintf( stderr, "Size of set header %d\n", sizeof( setHeader ) );
  numSets = indexSize / WAYS;
  if( numSets * WAYS < indexSize )
    {
      numSets += WAYS;
    }

  diskIndexSize = (sizeof( setHeader ) * numSets + 8192)/4096 * 4096;
  presentIndexOff = 0;
  headers = (setHeader*)(((unsigned)malloc( diskIndexSize ) + 4095) & ~(size_t)4095);

  //#define PRINT_HCFS_DEBUG
  fprintf( stderr, "%u\n", (unsigned)headers );
  //#endif

  for( i = 0; i < numSets; i++ )
    {
      for( j = 0; j < WAYS; j++ )
	{
	  setInteger( headers[ i ].clocks, j,  j );
	  headers[ i ].headers[ j ] =  0;
#ifdef COMPLETE_LOG
	  setRemOffset( &headers[ i ], j, -1 );
	  setRemVersion( &headers[ i ], j, -1 );
	  setRemLen( &headers[ i ], j, -1 );
#endif
	}

#ifdef PRINT_HCFS_DEBUG
      for( j = 0; j < WAYS; j++ )
	{
	  fprintf( stderr, "%u ", getInteger( headers[ i ].clocks, j ) );
	}

      fprintf( stderr, "\n" );	   
#endif
    }

  char indexFileName[ 100 ];
  int indexFd;
  int status;

  switch( mode )
    {
    case REUSEV:
      sprintf( indexFileName, "%s/index", indexFilePath );
      fprintf( stderr, "Reading index file: %s\n", indexFileName );
      indexFd = open( indexFileName, O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );
      status = pread( indexFd, (char*)headers, numSets * sizeof( setHeader ), 0 );
      close( indexFd );
      break;
    default:
      //for( i = 0; i < numSets; i++ )
	//headers[ i ].setNum = i;
      break;
    }

#endif

  fprintf( stderr, "Can index %d sets\n", numSets );
  numSetsUsed = 0;
  setUsage = calloc( numSets, sizeof( int ) );
  numDeletes = 0;

  return 0;
}

#ifndef COMPLETE_LOG
int updateHeaders( int hash, int matchNum, indexHeader header )
#else
int updateHeaders( unsigned int hash, int matchNum, indexHeader header, 
		    int remOffset, int remLen, int version )
#endif
{
  unsigned int setNum = getSetFromHash( hash );

  if( setUsage[ setNum ] == 0 )
    {
      numSetsUsed++;
    }

  setUsage[ setNum ] += 1;
  if( setUsage[ setNum ] > 8 )
    {
      numDeletes++;
    }
  
  if( setNum > numSets )
    {
      fprintf( stderr, "Bad set values %d %d\n", setNum, numSets );
      exit( -1 );
    }

  setHeader *presentHeaders;

#if LRU > 0
  if( hfind( setTab, (char*)(&setNum), sizeof( int ) ) == TRUE )
    {
      setIter( setList, hstuff( setTab ) );
      presentHeaders = (setHeader*)getPresentData( setList );
      setMatch( presentHeaders, matchNum, header );

#ifdef COMPLETE_LOG
      setRemOffset( presentHeaders, matchNum, remOffset );
#endif
    }
#endif

#if FULL > 0
  if( matchNum < 0 || matchNum > WAYS -1 )
    {
      exit( -1 );
    }

  presentHeaders = &headers[ setNum ];

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "match %d %u\n", matchNum, (size_t)(presentHeaders) );
#endif

  setMatch( presentHeaders, matchNum, header );

#ifdef PRINT_HCFS_DEBUG
  int i;

  for( i = 0; i < WAYS; i++ ) {
    fprintf( stderr, "%u ", getInteger( presentHeaders->clocks, i ) );
  }
  fprintf( stderr, "\nmatch %d %u\n", matchNum, (size_t)(presentHeaders) );
#endif

#ifdef COMPLETE_LOG
  setRemOffset( presentHeaders, matchNum, remOffset );
  setRemVersion( presentHeaders, matchNum, version );
  setRemLen( presentHeaders, matchNum, remLen );
#endif
#endif

  return 0;
}

int discardHeaders( int hash, int match )
{
  int setNum = getSetFromHash( hash );
  setHeader *presentSetHeader;
  int i;

#if FULL > 0
  presentSetHeader = &headers[ setNum ];
  
  int cut = getInteger( presentSetHeader->clocks, match );

  if( match < 0 || match > WAYS - 1 )
    exit( -1 );

  if( cut == 0 )
    {
      return 0;
    }

  presentSetHeader->headers[ match ] = -1 ;

  for( i = 0; i < WAYS; i++ )
    {
      if( getInteger( presentSetHeader->clocks, i ) < cut )
	{
	  incInteger( presentSetHeader->clocks, i );
	  continue;
	}

      if( getInteger( presentSetHeader->clocks, i ) == cut )
	{
	  setInteger( presentSetHeader->clocks, i, 0 );
	}
    }
#endif

  return 0;
} 
  
int updateUsage( int setNum )
{
#if LRU > 0
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Updating usage for set %d\n", setNum );
#endif

  if( hfind( setTab, (char*)(&setNum), sizeof( int ) ) == FALSE )
    {
      fprintf( stderr, "Attempt to update non existant entry\n" );
      return -1;
    }

  setIter( setList, hstuff( setTab ) );

  movePresentToTail( setList );
#endif

  return 0;
}

int updateIndexCache( int setNum, setHeader *newSetHeader )
{
#if LRU > 0
  setHeader *presentSetHeader;

  if( hfind( setTab, (char*)(&setNum), sizeof( int ) ) == TRUE )
    {
#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "Updating an existing set %d\n", setNum );
#endif

      setIter( setList, hstuff( setTab ) );
      presentSetHeader = (setHeader*)getPresentData( setList );
      memcpy( (void*)presentSetHeader, (void*)newSetHeader, sizeof( setHeader ) );
      movePresentToTail( setList );
      return 0;
    }

  startIter( setList );
  presentSetHeader = (setHeader*)getPresentData( setList );

  if( hfind( setTab, (char*)(&presentSetHeader->setNum), sizeof( int ) ) == TRUE )
    {
#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "Evicting %d from index\n", presentSetHeader->setNum );
#endif

      hdel( setTab );
    }

  memcpy( (void*)presentSetHeader, (void*)newSetHeader, sizeof( setHeader ) );

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Adding entry for set %d\n", presentSetHeader->setNum );
#endif

  hadd( setTab, (char*)(&presentSetHeader->setNum), sizeof( int ), (char*)getIter( setList ) );
	
  movePresentToTail( setList );

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Index stats: %d %d\n", (int)getSize( setList ), (int)setTab->count );
#endif

#endif

#if FULL > 0
  memcpy( (void*)(&headers[ setNum ]), (void*)newSetHeader, sizeof( setHeader ) );
#endif

  return 0;
}

inline void setMatch( setHeader *presentSetHeader, int match, indexHeader presentHeader )
{
  //  int num = *(int*)(&presentHeader );
  memcpy( &(presentSetHeader->headers[ match ]), &presentHeader, sizeof( hbits ) );
  updateClocks( presentSetHeader, match );
}

#ifdef COMPLETE_LOG
inline void setRemOffset( setHeader *presentSetHeader, int match, int remOffset )
{
  presentSetHeader->logOffset[ match ] = remOffset;
}

inline void setRemVersion( setHeader *presentSetHeader, int match, int remVersion )
{
  presentSetHeader->logVersion[ match ] = remVersion;
}

inline void setRemLen( setHeader *presentSetHeader, int match, int remLen )
{
  presentSetHeader->logLen[ match ] = remLen;
}

inline int getFirstBlockOffset( setHeader *presentSetHeader, int match )
{
  return presentSetHeader->logOffset[ match ];
}

inline int getFirstBlockVersion( setHeader *presentSetHeader, int match )
{
  return presentSetHeader->logVersion[ match ];
}

inline int getFirstBlockLen( setHeader *presentSetHeader, int match )
{
  return presentSetHeader->logLen[ match ];
}
#endif
setHeader* getSetHeader( int hash )
{
  unsigned int setNum = getSetFromHash( hash );
  if( setNum > numSets )
    {
      fprintf( stderr, "Bad set values %d %d\n", setNum, numSets );
      exit( -1 );
    }
#if LRU > 0
  setHeader *presentHeaders;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Looking up set %d\n", setNum );
#endif

  if( hfind( setTab, (char*)(&setNum), sizeof( int ) ) == TRUE )
    {
#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "Found\n" );
#endif

      setIter( setList, hstuff( setTab ) );
      presentHeaders = (setHeader*)getPresentData( setList );
      return presentHeaders;
    }

  return NULL;
#endif

#if FULL > 0
  return &headers[ setNum ];
#endif

  return NULL;
}

int updateClocks( setHeader *presentSetHeader, int match )
{
  int i;
  unsigned int cut = getInteger( presentSetHeader->clocks, match );
#ifdef PRINT_HCFS_DEBUG
  unsigned int total = 0;
#endif

  unsigned int present;

  if( match < 0 || match > WAYS - 1 )
    exit( -1 );

#ifdef PRINT_HCFS_DEBUG
  for( i = 0; i < WAYS; i++ ) {
    fprintf( stderr, "%u ", getInteger( presentSetHeader->clocks, i ) );
  }
  fprintf( stderr, "\n" );
#endif

  if( cut == WAYS - 1 )
    {
      return 0;
    }

  for( i = 0; i < WAYS; i++ )
    {
      present = getInteger( presentSetHeader->clocks, i );

#ifdef PRINT_HCFS_DEBUG
      total += present;
#endif

      if( present > cut )
	{
	  decInteger( presentSetHeader->clocks, i );
	  continue;
	}
      
      if( present == cut )
	{
	  setInteger( presentSetHeader->clocks, i, WAYS - 1 );
	}
    }

#ifdef PRINT_HCFS_DEBUG  
  for( i = 0; i < WAYS; i++ ) {
    fprintf( stderr, "%u ", getInteger( presentSetHeader->clocks, i ) );
  }
  
  if( total != 28 ) {
    exit( -1 );
  }

  fprintf( stderr, "\n\n" );
#endif

  return 0;
}

inline unsigned int getInteger( unsigned char *location, int where )
{
  return (unsigned int)(((unsigned long long)(*(unsigned long long*)location) & 
			 clockMasks[ where ] ) >> clockShifts[ where ]);
}

inline void setInteger( unsigned char *location, int where, unsigned int value )
{
  unsigned long long v = value;
  unsigned long long *l = (unsigned long long*)location;
  unsigned long long n = v << clockShifts[ where ];
  unsigned long long t = (*l ^ n) & clockMasks[ where ];
  *l = *l ^ t;
}

inline void decInteger( unsigned char *location, int where )
{
  unsigned int value = -1 + getInteger( location, where );
  setInteger( location, where, value );
}

inline void incInteger( unsigned char *location, int where )
{
  unsigned int value = 1 + getInteger( location, where );
  setInteger( location, where, value );
}

int isValid( setHeader *header )
{
#if LRU > 0
  if( header->setNum == -1 )
    {
      return -1;
    }
#endif

  return 0;
}

int hdrCmp( indexHeader hash, hbits cmp )
{
  //char actualHeader = cmp;

  /* if( memcmp( (char*)(&hash), (char*)(&cmp), sizeof( int ) ) == 0 )
    {
      return 0;
      }*/

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Comparing headers %u %u\n", hash, cmp );
#endif

  if( memcmp( &hash, &cmp, sizeof( hbits ) ) == 0 ) {
    //    fprintf( stderr, "header match\n" );
    return 0;
  }

  return -1;
}
	  
int cacheSet( unsigned int setNum, indexHeader *newIndexHeaders, double *ranks )
{
#if LRU > 0 || PARTIAL > 0
  setHeader *presentHeaderPtr;
  int i, k;
  int way = WAYS;
  char num;
  int num1;
#endif

#if LRU > 0
  if( hfind( setTab, (char*)(&setNum), sizeof( int ) ) == FALSE )
    {
      presentHeaderPtr = (setHeader*)getHeadData( setList );

#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "Deleting %d\n", presentHeaderPtr->setNum );
#endif

      if( hfind( setTab, (char*)(&(presentHeaderPtr->setNum)), sizeof( int ) ) == FALSE )
	{
	  fprintf( stderr, "Inconsistent state\n" );
	  return -1;
	}

      hdel( setTab );
    }
  else
    {
      return 0;
    }

  for( i = 0; i < WAYS; i++ )
    {
      num = *(char*)(&newIndexHeaders[ i ]);
      num1 = num;
      presentHeaderPtr->headers[ i ] = num1;
      k = whichMin( ranks, WAYS );
      setInteger( presentHeaderPtr->clocks, k, way-- );
      ranks[ k ] = DBL_MAX;
    }

  presentHeaderPtr->setNum = setNum;
  startIter( setList );
  hadd( setTab, (char*)(&(presentHeaderPtr->setNum)), sizeof( int ), getIter( setList ) );
  movePresentToTail( setList );
#endif
      
#if PARTIAL > 0
  presentHeaderPtr = (setHeader*)(&(headers[ setNum ]));

  for( i = 0; i < WAYS; i++ )
    {
      num = *(char*)(&newIndexHeaders[ i ]);
      num1 = num;
      presentHeaderPtr->headers[ i ] = num1;
      k = whichMin( ranks, WAYS );
      setInteger( presentHeaderPtr->clocks, k, way-- );
      ranks[ k ] = DBL_MAX;
    }
#endif
      
  return 0;
}

unsigned int getClock( setHeader *presentHeader, int i )
{
  return getInteger( presentHeader->clocks, i );
}

#if LRU > 0
int getSetNum( setHeader *presentHeader )
{
  return presentHeader->setNum;
}
#endif

hbits getIndexHeader( setHeader *presentHeader, int i )
{
  return presentHeader->headers[ i ];
}

int whichMin( double *ranks, int size )
{
  int i;
  double min = DBL_MAX;
  int which = 0;

  for( i = 0; i < size; i++ )
    {
      if( ranks[ i ] < min )
	{
	  min = ranks[ i ];
	  which = i;
	}
    }

  return which;
}

char* getIndexAddr()
{
  return (char*)headers;
}

int getIndexLen()
{
  return INDEX_CHUNK_SIZE;
}

off_t getIndexOff()
{
  off_t temp = presentIndexOff;

  presentIndexOff += INDEX_CHUNK_SIZE;

  if( presentIndexOff >= diskIndexSize ) {
    presentIndexOff = 0;
  }

  return temp;
}

void printSetStats()
{
  fprintf( stderr, "Num sets in use: %d\n", numSetsUsed );
  fprintf( stderr, "Num deleted: %d\n", numDeletes );
}

#endif
