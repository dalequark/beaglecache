#include "dirty.h"

#define ORIG 1
#define TEMP 2

struct dirtyObject {
  char* URL;
  list* allBlocks;
  void* iter;
  char which;
  char locks;
  char lDel;
  char used;
  char hoc;
};

static htab* dirtyCache;
static int numDirty;

int initDirtyCache()
{
  dirtyCache = hcreate( 10 );
  numDirty = 0;
  return 0;
}

dirtyObject* createDirtyObject( char* URL, list* allBlocks )
{
  dirtyObject* newDOB = (dirtyObject*)malloc( sizeof( dirtyObject ) );
  newDOB->URL = URL;
  newDOB->allBlocks = allBlocks;
  startIter( allBlocks );
  newDOB->iter = getIter( allBlocks );
  newDOB->which = ORIG;
  newDOB->locks = 1;
  newDOB->lDel = FALSE;
  newDOB->used = FALSE;
  newDOB->hoc = FALSE;
  numDirty++;

  if( getSize( allBlocks ) <= 0 )
    {
      fprintf( stderr, "Dirty object has no blocks!\n" );
      free( newDOB );
      numDirty--;
      exit( 0 );
      return NULL;
    }

  if( hfind( dirtyCache, URL, strlen( URL ) ) == TRUE )
    {
      free( newDOB );
      numDirty--;
      return NULL;
    }

  hadd( dirtyCache, URL, strlen( URL ), newDOB );

  return newDOB;
}

dirtyObject* getDirtyObject( char* URL )
{
  dirtyObject *newCopy, *oldCopy;

  if( hfind( dirtyCache, URL, strlen( URL ) ) == TRUE )
    {
      oldCopy = hstuff( dirtyCache );
      
      /* if( oldCopy->lDel == TRUE )
	{
	  return NULL;
	  }*/

      newCopy = malloc( sizeof( dirtyObject ) );
      memcpy( newCopy, oldCopy, sizeof( dirtyObject ) );
      oldCopy->locks += 1;
      newCopy->allBlocks = oldCopy->allBlocks;
      startIter( newCopy->allBlocks );
      newCopy->iter = getIter( newCopy->allBlocks );
      newCopy->which = TEMP;
      oldCopy->used = TRUE;
      newCopy->hoc = oldCopy->hoc;
      numDirty++;
      //fprintf( stderr, "%d blocks in this dirty object\n", getSize( newCopy->allBlocks ) );
      return newCopy;
    }
  
  return NULL;
}

char* getNextBlock( dirtyObject *thisDOB )
{
  char* dat;

  if( thisDOB->iter == NULL )
    return NULL;

  setIter( thisDOB->allBlocks, thisDOB->iter );
  dat = getPresentData( thisDOB->allBlocks );
  move( thisDOB->allBlocks );
  
  if( notEnd( thisDOB->allBlocks ) == FALSE )
    thisDOB->iter = NULL;
  else
    thisDOB->iter  = getIter( thisDOB->allBlocks );

  return dat;
}

int deleteDirtyObject( dirtyObject* thisDOB )
{
  dirtyObject* oldCopy;
  blockTailer* pTail;

  if( thisDOB == NULL )
    {
      return -1;
    }

  /*  fprintf( stderr, "Deleting %s dirty object %s with %d blocks\n", 
	   thisDOB->which==ORIG?"ORIG":"TEMP", thisDOB->URL, 
	   getSize( thisDOB->allBlocks ) );*/

  switch( thisDOB->which )
    {
    case ORIG:
      if( hfind( dirtyCache, thisDOB->URL, strlen( thisDOB->URL ) ) == TRUE )
	{
	  oldCopy = (dirtyObject*)hstuff( dirtyCache );
	  oldCopy->locks -= 1;
	}
      else
	{
	  fprintf( stderr, "Error in dirty cache state\n" );
	  return -1;
	}

      if( oldCopy->locks == 0 )
	{
	  hdel( dirtyCache );
	  numDirty--;

	  if( thisDOB->hoc == TRUE )
	    {
	      startIter( thisDOB->allBlocks );
	      
	      while( notEnd( thisDOB->allBlocks ) == TRUE )
		{
		  pTail = (blockTailer*)(getPresentData( thisDOB->allBlocks ) + FILE_BLOCK_SIZE - sizeof( blockTailer ) );
		  doneWriting( pTail->pres );
		  move( thisDOB->allBlocks );
		}
	    }

	  free( thisDOB );
	}
      else
	{
	  //	  hdel( dirtyCache );
	  thisDOB->lDel = TRUE;
	  return 1;
	}
      break;
    case TEMP:
      if( hfind( dirtyCache, thisDOB->URL, strlen( thisDOB->URL ) ) == TRUE )
	{
	  oldCopy = (dirtyObject*)hstuff( dirtyCache );
	  oldCopy->locks -= 1;
	}
      else
	{
	  fprintf( stderr, "Error in dirty cache state\n" );
	  return -1;
	}

      free( thisDOB );
      numDirty--;

      if( oldCopy->locks == 0 && oldCopy->lDel == TRUE )
	{
	  hdel( dirtyCache );
	  free( oldCopy->URL );
	  ldestroy( oldCopy->allBlocks );
	  numDirty--;

	  if( oldCopy->hoc == TRUE )
	    {
	      startIter( oldCopy->allBlocks );
	      
	      while( notEnd( oldCopy->allBlocks ) == TRUE )
		{
		  pTail = (blockTailer*)(getPresentData( oldCopy->allBlocks ) + FILE_BLOCK_SIZE - sizeof( blockTailer ) );
		  doneWriting( pTail->pres );
		  move( oldCopy->allBlocks );
		}
	    }

	  free( oldCopy );
	}

      break;
    }

  return 0;
}

int nowInHOC( dirtyObject* thisDOB )
{
  dirtyObject *oldCopy;

  if( hfind( dirtyCache, thisDOB->URL, strlen( thisDOB->URL ) ) == TRUE )
    {
      oldCopy = hstuff( dirtyCache );
      
      if( oldCopy->hoc == TRUE )
	{
	  return TRUE;
	}
    }

  return FALSE;
}

int reAssignBlocks( dirtyObject* thisDOB, list* newBlocks )
{
  thisDOB->hoc = TRUE;
  //  fprintf( stderr, "Reassign %d %d %s!!\n", getSize( thisDOB->allBlocks ), getSize( newBlocks ), thisDOB->URL );
  startIter( thisDOB->allBlocks );
  startIter( newBlocks );

  if( getSize( thisDOB->allBlocks ) != 0 )
    {
      startIter( thisDOB->allBlocks );

      while( notEnd( thisDOB->allBlocks ) == TRUE )
	{
	  setPresentData( thisDOB->allBlocks, getPresentData( newBlocks ) );
	  move( thisDOB->allBlocks );
	  move( newBlocks );
	}
    }
  
  return 0;
}

int ifUsed( dirtyObject *thisDOB )
{
  return thisDOB->used;
}

int getNumDirty()
{
  return numDirty;
}

int getNumBlocks( dirtyObject* ob )
{
  return getSize( ob->allBlocks );
}
