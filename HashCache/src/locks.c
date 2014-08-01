#include "locks.h"

static htab* locks;

int initLocks()
{
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Creating a table of file locks\n" );
#endif

  locks = hcreate( 7 );

  if( locks == NULL )
    {
      fprintf( stderr, "Error creating a table of file locks\n" );
      return -1;
    }

  return 0;
}

int addReadLock( char *URL, int hash, int match )
{
  int status;
  countLock *presentLockPtr;
  int *lockData = (int*)malloc( 8 );

  lockData[ 0 ] = getSetFromHash( hash );
  lockData[ 1 ] = match;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Adding read lock to %s %d %d\n", URL, hash, match );
#endif

  if( hfind( locks, (char*)(lockData), 8 ) == TRUE )
    {
      presentLockPtr = hstuff( locks );

      presentLockPtr->reads++;

      free( lockData );
      return 0;
    }
  else
    {
      presentLockPtr = malloc( sizeof( countLock ) );

      presentLockPtr->reads = 1;
      presentLockPtr->write = 0;
      presentLockPtr->data = (char*)lockData;

      status = hadd( locks, (char*)(lockData), 8, (char*)presentLockPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a new lock\n" );
	  return -1;
	}
    }

  return 0;
}

int addWriteLock( char *URL, int hash, int match )
{
  int status;
  countLock *presentLockPtr;

  int *presentLocation = malloc( 2 * sizeof(int) );
  presentLocation[ 0 ] = getSetFromHash( hash );
  presentLocation[ 1 ] = match;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Adding write lock to %s\n", URL );
#endif

  if( hfind( locks, (char*)(presentLocation), 8 ) == TRUE )
    {
      presentLockPtr = hstuff( locks );

      if( presentLockPtr->reads == 0 )
	{	  
	  return 0;
	}
      else
	{
	  return 1;
	}

      free( presentLocation );
    }
  else
    {
      presentLockPtr = malloc( sizeof( countLock ) );

      presentLockPtr->reads = 0;
      presentLockPtr->write = 1;
      presentLockPtr->data = (char*)presentLocation;

      status = hadd( locks, (char*)(presentLocation), 8, (char*)presentLockPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a new lock\n" );
	  return -1;
	}
    }

  return 0;
}

int removeReadLock( char *URL, int hash, int match )
{
  countLock *presentLockPtr;
  int presentLocation[ 2 ];
  presentLocation[ 0 ] = getSetFromHash( hash );
  presentLocation[ 1 ] = match;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Removing read lock on %s %d %d\n", URL, hash, match );
#endif

  if( hfind( locks, (char*)(presentLocation), 8 ) == TRUE )
    {
      presentLockPtr = (countLock*)hstuff( locks );

      if( presentLockPtr->reads == 0 )
	{
	  fprintf( stderr, "Removing a lock on a free object\n" );
	  return -1;
	}
      else
	{
	  presentLockPtr->reads--;

	  if( presentLockPtr->reads == 0 && presentLockPtr->write == 0 )
	    {
	      hdel( locks );

	      free( presentLockPtr->data );
	      free( presentLockPtr );
	      
	      presentLockPtr = NULL;
	    }

	  return 0;
	}
    }
  else
    {
      fprintf( stderr, "No read locks have been put on this object\n" );
      return -1;
    }

  return 0;
}

int removeWriteLock( char *URL, int hash, int match )
{
  countLock *presentLockPtr;
  int presentLocation[ 2 ];
  presentLocation[ 0 ] = getSetFromHash( hash );
  presentLocation[ 1 ] = match;

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Removing write lock on %s\n", URL );
#endif

  if( hfind( locks, (char*)(presentLocation), 8 ) == TRUE )
    {
      presentLockPtr = hstuff( locks );

      if( presentLockPtr->write == 0 )
	{
	  fprintf( stderr, "Removing a lock on a free object\n" );
	  return -1;
	}
      else
	{
	  presentLockPtr->write--;

	  if( presentLockPtr->write == 0 && presentLockPtr->reads == 0 )
	    {
	      hdel( locks );

	      free( presentLockPtr->data );
	      free( presentLockPtr );
	      presentLockPtr = NULL;
	    }
	  else
	    {
	      fprintf( stderr, "There seems to have been a write collisions\n" );
	      return -1;
	    }

	  return 0;
	}
    }
  else
    {
      //      fprintf( stderr, "No write locks have been put on this object\n" );
      return -1;
    }

  return 0;
}

int lockOn( int hash, int match )
{
  int lockData[ 2 ];
  lockData[ 0 ] = getSetFromHash( hash );
  lockData[ 1 ] = match;

  if( hfind( locks, (char*)(lockData), 8 ) == TRUE )
    {
      return TRUE;
    }

  return FALSE;
}

int lockCount()
{
  return (int)locks->count;
}
