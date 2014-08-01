#include "hcfs.h"
#include <error.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define THRESHOLD 1000
#define INFO_LEN 8

static int *hcFds;
static int cacheSize;
static int FIRST_FILE_SIZE;
static int REM_FILE_SIZE;
static int presentRemVersion;
static int* presentRemOffsets;
static int presentDisk;
static int allocStart;
int numDisks;
static int SUB_FIRST_FILE_SIZE;
static int SUB_REM_FILE_SIZE;
extern int onDiskIndexSize;
extern unsigned int numSets;
extern char* indexFilePath;
static int* logFileLength;
int initIndex( int indexSize, int mode );
int cacheHeaders( int hash, blockHeader **presentHeaders );
extern int direct;

int initHCFS( int num, int size, char **datPaths, int mode, int numDisksGiven, 
	      int indexSize )
{
  char fileName[ 100 ];
  int i, k, count, offFd;
  char *data = malloc( 2 * FILE_BLOCK_SIZE );
  char *aligned = (char*)( ( ( (size_t)data / FILE_BLOCK_SIZE ) * FILE_BLOCK_SIZE ) );
  if( (unsigned)aligned < (unsigned)data )
    aligned += FILE_BLOCK_SIZE;

  int status;
  off_t offset = 0;
  blockHeader *pHead[ WAYS ];

  logFileLength = malloc( sizeof( int ) * numDisksGiven );

  for( i = 0; i < numDisksGiven; i++ )
    {
      logFileLength[ i ] = i;
    }

  char buffer1[ FILE_BLOCK_SIZE * (WAYS+1) ];
  char *buffer = (char*)( ( ( (size_t)buffer1 / FILE_BLOCK_SIZE ) * FILE_BLOCK_SIZE ));
  if( (unsigned)buffer < (unsigned)buffer1 )
    buffer += FILE_BLOCK_SIZE;

  numDisks = numDisksGiven;

#ifndef COMPLETE_LOG
  FIRST_FILE_SIZE = ( num / numDisks ) * numDisks;
  
  if( FIRST_FILE_SIZE < num )  {
    FIRST_FILE_SIZE += numDisks;
  }
#else
  FIRST_FILE_SIZE = 0;
#endif

  cacheSize = size * 1024 * 1024 / ( FILE_BLOCK_SIZE / 1024 );

  REM_FILE_SIZE = ( ( cacheSize - FIRST_FILE_SIZE ) / numDisks ) * numDisks;

  fprintf( stderr, "Initiating %d blocks %d objects\n", REM_FILE_SIZE, 
	   num );

  presentRemVersion = 0;
  presentRemOffsets = malloc( numDisks * sizeof( int ) );
  //  presentRemVersions = malloc( numDisks * sizeof( int ) );

  for( i = 0; i < numDisks; i++ ) {
#ifndef COMPLETE_LOG
    presentRemOffsets[ i ] = FIRST_FILE_SIZE + i;
#else
    presentRemOffsets[ i ] = i;
#endif
  }
  presentRemVersion = 0;

  presentDisk = 0;
  allocStart = 0;

  pHead[ 0 ] = (blockHeader*)aligned;
  pHead[ 0 ]->key.tv_sec = 0;
  SUB_FIRST_FILE_SIZE = FIRST_FILE_SIZE / numDisks;
  SUB_REM_FILE_SIZE = REM_FILE_SIZE / numDisks;
  hcFds = (int*)malloc( sizeof( int ) * numDisks );

#if FULL > 0
  indexSize = num / numDisks * numDisks;
#endif

  status = initIndex( indexSize, mode );
  
  if( status < 0 )
    {
      fprintf( stderr, "Cannot init index\n" );
      return -1;
    }

  switch( mode )
    {
    case CREATE:
      for( k = 0; k < numDisks; k++ )
	{ 
	  if( direct == FALSE )
	    sprintf( fileName, "%s/HCFS", datPaths[ k ] );
	  else
	    sprintf( fileName, "%s", datPaths[ k ] );

	  if( direct == FALSE )
	    hcFds[ k ] = open( fileName, O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );
	  else
	    hcFds[ k ] = open( fileName, O_RDWR );

	  if( hcFds[ k ] < 0 )
	    {
	      fprintf( stderr, "Cannot open %s\n", fileName );
	      return -1;
	    }

#ifdef PRINT_HCFS_DEBUG
	  fprintf( stderr, "HCFS file descriptor: %d\n", hcFds[ k ] );
#endif

	  offset = 0;
	  for( i = 0; i < SUB_FIRST_FILE_SIZE + SUB_REM_FILE_SIZE; i++ )
	    {
	      status = pwrite( hcFds[ k ], aligned, onDiskIndexSize, offset );
	      offset += onDiskIndexSize;
	      
	      if( status < onDiskIndexSize )
		{
		  fprintf( stderr, "Not able to format HCFS - written %d\n", status );
		  
		  if( status == -1 )
		    {
		      fprintf( stderr, "%s\n", strerror( errno ) );
		    }

		  return -1;
		}
	      //printf( "." );
	    }

	  close( hcFds[ k ] );

	  if( direct == FALSE )
	    hcFds[ k ] = open( fileName, O_DIRECT | O_CREAT | O_RDWR | 
			       O_LARGEFILE, S_IRWXU );
	  else
	    hcFds[ k ] = open( fileName, O_DIRECT | O_RDWR | O_LARGEFILE );
	}
      break;
    case OPEN:
      for( k = 0; k < numDisks; k++ ) {
	if( direct == FALSE )
	  sprintf( fileName, "%s/HCFS", datPaths[ k ] );
	else
	  sprintf( fileName, "%s", datPaths[ k ] );
	
	if( direct == FALSE )
	  hcFds[ k ] = open( fileName, O_DIRECT | O_CREAT | O_RDWR | 
			     O_LARGEFILE, S_IRWXU );
	else
	  hcFds[ k ] = open( fileName, O_DIRECT | O_RDWR | O_LARGEFILE );
      }
      break;
    case REUSEV:
      sprintf( fileName, "%s/offsets", indexFilePath );
      offFd = open( fileName, O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );
      count = 0;
      
      count += pread( offFd, &presentRemVersion, sizeof( int ), count );

      fprintf( stderr, "Previous Rem Version %d\n", presentRemVersion );

      for( k = 0; k < numDisks; k++ )
	{
	  if( direct == FALSE )
	    sprintf( fileName, "%s/HCFS", datPaths[ k ] );
	  else
	    sprintf( fileName, "%s", datPaths[ k ] );
	  
	  if( direct == FALSE )
	    hcFds[ k ] = open( fileName, O_DIRECT | O_CREAT | O_RDWR | 
			       O_LARGEFILE, S_IRWXU );
	  else
	    hcFds[ k ] = open( fileName, O_DIRECT | O_RDWR | O_LARGEFILE );

	  if( hcFds[ k ] < 0 )
	    {
	      return -1;
	    }

	  count += pread( offFd, &presentRemOffsets[ k ], sizeof( int ), count );

	  fprintf( stderr, "Previous Rem Offset %d: %d\n", k, presentRemOffsets[ k ] );
	}      
      close( offFd );
      break;
    default:
      return -1;
    }
  
  return 0;
}

int convertRelToAbsRead( int block, unsigned int hash, int match, 
			 int remOffset, int readLen, absLocs *presentAbsPtr )
{
  //long arg;
  int objFd;
  size_t mapsize;
  off_t offset;
  int k;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "offset size %d\n", sizeof( off_t ) );
  fprintf( stderr, "Need to convert %d with hash %d\n", block, hash );
#endif

  switch( block )
    {
    case 1:
      {
#ifndef COMPLETE_LOG
	if( match != -1 )
	  remOffset = getSetFromHash( hash ) * WAYS + match;
	else
	  remOffset = getSetFromHash( hash ) * WAYS;
#endif
	k = whichRem( remOffset );
	objFd = hcFds[ k ];
	mapsize = readLen;
	offset = (off_t)( ( remOffset - k )/ numDisks )* (off_t)FILE_BLOCK_SIZE;
	break;
      }
    default:
      {
	k = whichRem( remOffset );
	objFd = hcFds[ k ];
	mapsize = readLen;
	offset = (off_t)( ( remOffset - k )/ numDisks )* (off_t)FILE_BLOCK_SIZE;
	break;
      }
    }

  presentAbsPtr->offset = offset;
  presentAbsPtr->objFd = objFd;
  presentAbsPtr->mapsize = mapsize;
  presentAbsPtr->remOffset = remOffset;

  return 0;
}

int convertRelToAbsWrite( unsigned int hash, int match, int block, 
			  int remOffset, int writeLen, absLocs *presentAbs )
{
  off_t offset;
  int k;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Need to convert hash: %d match:"\
	   " %d block: %d size: %d for write\n", hash, match, block, writeLen );
#endif

  if( writeLen != -1 )
    presentAbs->mapsize = writeLen;
  else
    presentAbs->mapsize = FILE_BLOCK_SIZE;

  switch( block )
    {
    case 1:
      {
#ifndef COMPLETE_LOG
	remOffset = getSetFromHash( hash ) * WAYS + match;
#endif

	k = whichRem( remOffset );
	presentAbs->objFd = hcFds[ k ];
	presentAbs->remOffset = remOffset;
	remOffset = ( remOffset - k ) / numDisks;
	offset = (off_t)remOffset * (off_t)FILE_BLOCK_SIZE;

	break;
      }
    default:
      {
	k = whichRem( remOffset );
	presentAbs->objFd = hcFds[ k ];
	presentAbs->remOffset = remOffset;
	remOffset = ( remOffset - k ) / numDisks;
	offset = (off_t)remOffset * (off_t)FILE_BLOCK_SIZE;
	break;
      }
    }

  presentAbs->offset = offset;

  return 0;
}

int getRemVersion()
{
  return presentRemVersion;
}

int checkVersion( int version, int offset )
{
  if( version == presentRemVersion )
    {
      return 0;
    }

  if( version == presentRemVersion - 1 && 
      offset > presentRemOffsets[ 0 ] + THRESHOLD )
    {
      return 0;
    }

  //  fprintf( stderr, "Version mismatch\n" );

  return -1;
}

inline unsigned int getSetFromHash( unsigned int hash )
{
  return hash % numSets;
}

inline int getOffsetFromHash( unsigned int hash, int match )
{
  return getSetFromHash( hash ) * WAYS + match;
}

inline int whichFirst( int setNum )
{
  if( numDisks == 1 )
    {
      return 0;
    }

  return setNum % numDisks;
}

inline int whichRem( int remOffset )
{
  if( numDisks == 1 )
    {
      return 0;
    }

  return remOffset % numDisks;
}

inline int getFromOneDisk( int numBlocks, int* start, int* version, 
			   int* diskNum, int* leftInCycle )
{
  *diskNum = presentDisk;
  int i;

  if( presentRemOffsets[ presentDisk ] + ( numBlocks * numDisks ) > REM_FILE_SIZE )
    {
      presentRemVersion += 1;
      *version = presentRemVersion;
      allocStart += numBlocks;

      for( i = 0; i < numDisks; i++ )
	presentRemOffsets[ i ] = FIRST_FILE_SIZE + i;

      *start = presentRemOffsets[ presentDisk ];
      presentRemOffsets[ presentDisk ] += ( numBlocks * numDisks );
    }
  else
    {
      allocStart += numBlocks;
      *version = presentRemVersion;
      *start = presentRemOffsets[ presentDisk ];
      presentRemOffsets[ presentDisk ] += ( numBlocks * numDisks );
    }

  if( CYCLE_CHUNKS - allocStart > 0 )
    {
      *leftInCycle = CYCLE_CHUNKS - allocStart;
    }
  else
    {
      *leftInCycle = 0;
    }

  if( allocStart >= CYCLE_CHUNKS )
    {
      presentDisk = presentDisk + 1;
      allocStart = 0;
    }

  if( presentDisk >= numDisks )
    {
      presentDisk = 0;
    }

  return 0;
}

int beginAllocCycle()
{
  if( allocStart == 0 )
    return 0;

  allocStart = 0;

  presentDisk = presentDisk + 1;

  if( presentDisk >= numDisks )
    {
      presentDisk = 0;
    }

  return 0;
}

int getRemOffsetDummy()
{
  return presentRemOffsets[ 0 ];
}

int getRemOffset( int i )
{
  return presentRemOffsets[ i ];
}

void setLogFileLength( int len )
{
  if( logFileLength[ whichRem( len ) ] < REM_FILE_SIZE && 
      len > logFileLength[ whichRem( len ) ] )
    logFileLength[ whichRem( len ) ] = len;
}

char checkLogFileLength( int len )
{
  if( logFileLength[ whichRem( len ) ] <= len )
    return 0;

  return 1;
}

void printLogFileLengths()
{
  int i;

  for( i = 0; i < numDisks; i++ )
    {
      fprintf( stderr, "Disk %d length %d\n", i, logFileLength[ i ] );
    }
}
