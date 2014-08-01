#include "hoc.h"

struct hotObject {
  char *block;
  char readLocks;
  char writeLocks;
  hoKey thisKey;
};

static htab* whichBlocks;
static list* allBlocks;
static list* freeList;
static int expandCount;
static int expandLimit;
static int expandHOC();
extern int numDisks;
static int globalReadLocks;
static int globalWriteLocks;

int initHOC( int nExpandLimit )
{
  fprintf( stderr, "Initiating HOC with limit %d\n", nExpandLimit );
  expandLimit = nExpandLimit;
  expandCount = 0;
  whichBlocks = hcreate( 10 );
  allBlocks = create();
  freeList = create();
  expandHOC();
  globalReadLocks = 0;
  globalWriteLocks = 0;
  return 0;
}

int printHOCStats()
{
  fprintf( stderr, "Num objects in list: %d, table: %d reads: %d writes: %d\n", (int)hcount( whichBlocks ), getSize( allBlocks ), globalReadLocks, globalWriteLocks );
  return 0;
}

hotObject *insertBlock( char* dat, int remOffset )
{
  hoKey present;
  hotObject *newHOB;
  present[ 0 ] = remOffset;

  if( hfind( whichBlocks, (char*)(present), sizeof( hoKey ) ) == TRUE )
    {
      //      fprintf( stderr, "." );
      setIter( allBlocks, hstuff( whichBlocks ) );
      newHOB = (hotObject*)getPresentData( allBlocks );
      movePresentToTail( allBlocks );
      //      memcpy( newHOB->block, dat, FILE_BLOCK_SIZE );
      return newHOB;
    }
  else
    {
      newHOB = getOldHOB();
    }

  if( newHOB == NULL )
    {
      expandHOC();
      newHOB = getOldHOB();
    }

  memcpy( newHOB->block, dat, FILE_BLOCK_SIZE );
  newHOB->thisKey[ 0 ] = remOffset;

  addToTail( allBlocks, 0, ((char*)newHOB ) );
  endIter( allBlocks );
  hadd( whichBlocks, (char*)newHOB->thisKey, sizeof( hoKey ), getIter( allBlocks ) );
  
  if( newHOB->readLocks > 0 || newHOB->writeLocks > 0 )
    {
      globalReadLocks -= newHOB->readLocks;
      globalWriteLocks -= newHOB->writeLocks;
      fprintf( stderr, "Out of memory\n" );
      exit( -1 );
    }

  newHOB->readLocks = 0;
  newHOB->writeLocks = 0;

  return newHOB;
}

static int expandHOC()
{
  int i;
  char *buffer = malloc( EXPAND_SIZE * FILE_BLOCK_SIZE );
  char* base = buffer;
  hotObject* newHOB;
  expandCount++;

  if( buffer == NULL )
    {
      fprintf( stderr, "Cannot malloc\n" );
      exit( -1 );
    }

  //fprintf( stderr, "%d\n", FILE_BLOCK_SIZE );

  for( i = 0; i < EXPAND_SIZE; i++ )
    {
      newHOB = (hotObject*)malloc( sizeof( hotObject ) );
      newHOB->block = base;
      newHOB->readLocks = 0;
      newHOB->writeLocks = 0;
      base += FILE_BLOCK_SIZE;
      addToHead( freeList, 0, (char*)newHOB );
    }

  if( (unsigned)base > (unsigned)buffer + (EXPAND_SIZE)*FILE_BLOCK_SIZE )
    {
      fprintf( stderr, "Error in HOC expansion\n" );
      exit( -1 );
    }

  return 0;
}

hotObject *deleteLRU()
{
  hotObject* oldHOB;
  int attempts = 0;

  while( attempts < 3 )
    {
      oldHOB = (hotObject*)getHeadData( allBlocks );
  
      if( hfind( whichBlocks, oldHOB->thisKey, sizeof( hoKey ) ) == FALSE )
	{
	  fprintf( stderr, "Error in HOC\n" );
	  exit( -1 );
	}
      else
	{
	  setIter( allBlocks, hstuff( whichBlocks ) );
	  oldHOB = (hotObject*)getPresentData( allBlocks );
      
	  if( oldHOB->readLocks > 0 || oldHOB->writeLocks > 0 )
	    {
	      attempts++;
	      movePresentToTail( allBlocks );
	      continue;
	    }
	  
	  hdel( whichBlocks );
	  deletePresent( allBlocks );
	  return oldHOB;
	}
    }
    
  return NULL;
}

char *getBlock( int remOffset )
{
  hotObject* oldHOB;
  hoKey thisKey;
  thisKey[ 0 ] = remOffset;

  if( hfind( whichBlocks, (char*)thisKey, sizeof( hoKey ) ) == TRUE )
    {
      globalReadLocks++;
      setIter( allBlocks, hstuff( whichBlocks ) );
      oldHOB = (hotObject*)getPresentData( allBlocks );
      movePresentToTail( allBlocks );
      oldHOB->readLocks++;
      //      fprintf( stderr, "%d %d\n", oldHOB->readLocks, oldHOB->writeLocks );
      return oldHOB->block;
    }

  return NULL;
}

int doneReading( int remOffset  )
{
  hotObject* oldHOB;
  hoKey thisKey;
  thisKey[ 0 ] = remOffset;

  if( hfind( whichBlocks, (char*)thisKey, sizeof( hoKey ) ) == TRUE )
    {
      globalReadLocks--;
      setIter( allBlocks, hstuff( whichBlocks ) );
      oldHOB = (hotObject*)getPresentData( allBlocks );
      oldHOB->readLocks--;
      return 0;
    }

  return -1;
}

int doneWriting( int remOffset )
{
  hotObject* oldHOB;
  hoKey thisKey;
  thisKey[ 0 ] = remOffset;

  if( hfind( whichBlocks, (char*)thisKey, sizeof( hoKey ) ) == TRUE )
    {
      setIter( allBlocks, hstuff( whichBlocks ) );
      oldHOB = (hotObject*)getPresentData( allBlocks );
      oldHOB->writeLocks--;
      globalWriteLocks--;
      hdel( whichBlocks );
      oldHOB->writeLocks = 0;
      oldHOB->readLocks = 0;
      deletePresent( allBlocks );
      addToTail( freeList, 0, (char*)oldHOB );
      return 0;
    }

  return -1;
}

#ifdef COMPLETE_LOG
int getKBlocks( list* blockList, int k, int offset )
#else
int getKBlocks( list* blockList, int k, int first, int offset )
#endif
{
  int i;
  hoKey thisKey;
  hotObject* thisHOB;

#ifndef COMPLETE_LOG
  thisKey[ 0 ] = first;

  if( hfind( whichBlocks, (char*)thisKey, sizeof( hoKey ) ) == TRUE )
    {
      setIter( allBlocks, hstuff( whichBlocks ) );
      thisHOB = (hotObject*)getPresentData( allBlocks );
      addToTail( blockList, 0, thisHOB->block );
	  thisHOB->writeLocks++;
    }
  else
    {
      thisHOB = getOldHOB();
      memcpy( thisHOB->thisKey, thisKey, sizeof( hoKey ) );
      addToTail( blockList, 0, thisHOB->block );
      addToTail( allBlocks, 0, (char*)thisHOB );
      endIter( allBlocks );
      
      hadd( whichBlocks, (char*)thisHOB->thisKey, sizeof( hoKey ), 
	    getIter( allBlocks ) );
      thisHOB->writeLocks++;
    }
  
  globalWriteLocks++;
#endif

  for( i = 0; i < k; i++ )
    {
      thisKey[ 0 ] = offset + ( i * numDisks );

      if( hfind( whichBlocks, (char*)thisKey, sizeof( hoKey ) ) == TRUE )
	{
	  setIter( allBlocks, hstuff( whichBlocks ) );
	  thisHOB = (hotObject*)getPresentData( allBlocks );
	  addToTail( blockList, 0, thisHOB->block );
	  thisHOB->writeLocks++;
	}
      else
	{
	  thisHOB = getOldHOB();
	  memcpy( thisHOB->thisKey, thisKey, sizeof( hoKey ) );
	  addToTail( blockList, 0, thisHOB->block );
	  addToTail( allBlocks, 0, (char*)thisHOB );
	  endIter( allBlocks );
	
	  hadd( whichBlocks, (char*)thisHOB->thisKey, sizeof( hoKey ), getIter( allBlocks ) );
	  thisHOB->writeLocks++;
	}

      globalWriteLocks++;
    }
      
  return 0;
}

hotObject* getOldHOB( void )
{
  hotObject *oldHOB;

  if( getSize( freeList ) > 0 )
    {
      oldHOB = (hotObject*)getHeadData( freeList );
      deleteHead( freeList );
    }
  else
    {
      if( expandCount < expandLimit )
	{
	  expandHOC();
	  oldHOB = (hotObject*)getHeadData( freeList );
	  deleteHead( freeList );
	}
      else
	{
	  oldHOB = deleteLRU();
	}
    }

  if( oldHOB == NULL )
    {
      expandHOC();
      oldHOB = (hotObject*)getHeadData( freeList );
      deleteHead( freeList );
    }
  
  return oldHOB;
}

int getReadLockCount()
{
  return globalReadLocks;
}

int getWriteLockCount()
{
  return globalWriteLocks;
}

