#include "conn.h"

static htab *connTab;

int initConnectionBase()
{
  connTab = hcreate( 10 );
  return 0;
}

connection* getConnectionFromBase( char *server, int port )
{
  baseEntry *presentConns;
  connection *presentConn;

  if( hfind( connTab, server, strlen( server ) ) == TRUE )
    {
      presentConns = hstuff( connTab );
      
      startIter( presentConns->conns );

      while( notEnd( presentConns->conns ) == TRUE )
	{
	  presentConn = (connection*)getPresentData( presentConns->conns );

	  if( presentConn->inUse != TRUE && port == presentConn->port )
	    {
	      presentConn->inUse = TRUE;
#ifdef PRINT_PROXY_DEBUG
	      fprintf( stderr, "Reuse %s\n", server );
#endif
	      
	      gettimeofday( &presentConn->used, NULL );
	      return presentConn;
	    }

	  move( presentConns->conns );
	}

      return NULL;
    }
  else
    {
      return NULL;
    }
}
  
int addConnectionToBase( connection *newConn )
{
  baseEntry *presentConns;
  
  if( newConn == NULL || newConn->server == NULL )
    {
      return -1;
    }

  if( newConn->added == TRUE )
    {
      return -1;
    }

  newConn->added = TRUE;
  gettimeofday( &newConn->used, NULL );

  if( hfind( connTab, newConn->server, strlen( newConn->server ) ) == TRUE )
    {
      presentConns = hstuff( connTab );
      addToHead( presentConns->conns, 0, (char*)newConn );
      return 0;
    }
  else
    {
      presentConns = malloc( sizeof( baseEntry ) );
      strcpy( presentConns->server, newConn->server );
      presentConns->conns = create();
      addToHead( presentConns->conns, 0, (char*)newConn );
      hadd( connTab, presentConns->server, strlen( newConn->server ), 
	    (char*)presentConns );
      return 0;
    }

  return 0;
}

int delConnectionFromBase( connection *oldConn )
{
  baseEntry *presentConns;
  connection* presentConn;

  if( oldConn == NULL || oldConn->server == NULL )
    {
      return -1;
    }

  if( hfind( connTab, oldConn->server, strlen( oldConn->server ) ) == TRUE )
    {
      presentConns = hstuff( connTab );

      if( presentConns != NULL )
	{
	  startIter( presentConns->conns );

	  while( notEnd( presentConns->conns ) == TRUE )
	    {
	      presentConn = (connection*)getPresentData( presentConns->conns );

	      if( (unsigned)presentConn == (unsigned)oldConn )
		{
		  deletePresent( presentConns->conns );
		  break;
		}

	      move( presentConns->conns );
	    }

	  if( getSize( presentConns->conns ) == 0 )
	    {
	      ldestroy( presentConns->conns );
	      free( presentConns );
	      hdel( connTab );
	    }
	}

      return 0;
    }

  return -1;
}

void closeOldConnections() 
{
  baseEntry *presentConns;
  connection* presentConn;

  struct timeval now;
  gettimeofday( &now, NULL );

  if( hfirst( connTab ) ) do {
      presentConns = hstuff( connTab );
      
      if( presentConns != NULL ) {
	startIter( presentConns->conns );
	
	while( notEnd( presentConns->conns ) == TRUE ) {
	  presentConn = (connection*)getPresentData( presentConns->conns );
	  
	  if( presentConn->used.tv_sec + EXPIRY_TIME <
	      now.tv_sec && presentConn->inUse != TRUE ) {
	    delConnectionPerm( presentConn );
	    deletePresent( presentConns->conns );
	  }
	  
	  move( presentConns->conns );
	}
	
	if( getSize( presentConns->conns ) == 0 ) {
	  ldestroy( presentConns->conns );
	  free( presentConns );
	  hdel( connTab );
	}
      } else {
	hdel( connTab );
      }
    } while( hnext( connTab ) );
}
	  
