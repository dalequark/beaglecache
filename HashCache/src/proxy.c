#include "proxy.h"

float timeVal( struct timeval *timeNow )
{
  return ((float)timeNow->tv_sec + (float)timeNow->tv_usec / 1000000);
}

int getBestMatch( httpResponse *presentRTranPtr, blockHeader **pHead, char **aData, int *match )
{
#ifdef PRINT_PROXY_DEBUG
  stamp now1, now2;
  gettimeofday( &now1, NULL );
#endif

  int i;
  int best = -1;
  asynckey bestTime;
  bestTime.tv_sec = -1;
  bestTime.tv_usec = -1;
  int len = strlen( presentRTranPtr->URL );

  if( len + 1 > URL_SIZE )
    len = URL_SIZE - 1;

#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "Finding the best match for the URL %s\n", presentRTranPtr->URL );
#endif

  for( i = 0; i < WAYS; i++ )
    {
#ifdef PRINT_PROXY_DEBUG
      //  fprintf( stderr, "%100s\n", pHead[ i ]->URL );
#endif

      if( strncmp( presentRTranPtr->URL, pHead[ i ]->URL, len ) == 0 )
	{
#ifdef PRINT_PROXY_DEBUG
	  fprintf( stderr, "Match for %s is %s\n", presentRTranPtr->URL, pHead[ i ]->URL );
#endif

	  if( pHead[ i ]->key.tv_sec > bestTime.tv_sec )
	    {
	      best = i;
	      bestTime.tv_sec = pHead[ i ]->key.tv_sec;
	    }
	}
    }

  *match = best;

#ifdef PRINT_PROXY_DEBUG
  gettimeofday( &now2, NULL );
  //  fprintf( stderr, "Best Match Time %ld\n", (now2.tv_sec - now1.tv_sec)*1000+( now2.tv_usec - now1.tv_usec )/1000);
#endif

  return 0;
}

int getEmptyBlock( blockHeader **pHead, int hash, int *match )
{
#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Fetching an empty block\n" );
#endif

  int i;
  int best = -1;
  asynckey bestTime;

  gettimeofday( &bestTime, NULL );

  for( i = 0; i < WAYS; i++ )
    {
      if( lockOn( hash, i ) == TRUE )
	{
	  continue;
	}

      if( pHead[ i ]->key.tv_sec < bestTime.tv_sec )
	{
	  best = i;
	  bestTime.tv_sec = pHead[ i ]->key.tv_sec;
	}
    }

  //  fprintf( stderr, "time %ld best %d\n", bestTime.tv_sec, best );

  *match = best;

  return 0;
}

int delResponseTranPerm( httpResponse *presentRTranPtr )
{
  int status = 0;
#ifdef PRINT_PROXY_DEBUG
  //fprintf( stderr, "P res del %d\n", presentRTranPtr->tfd );
#endif

  //  char *message = getPutOK();

  /*  if( presentRTranPtr->tfd != -1 && presentRTranPtr->method == WAPROX_PUT )
    {
      write( presentRTranPtr->tfd, message, 18 );
      close( presentRTranPtr->tfd );
      }*/
  //  connection *presentConn;

  bufBlock *presentBufBlock;
  presentBufBlock = NULL;
  startIter( presentRTranPtr->blocks );
  //fprintf( stderr, "hi8\n" );
  //printf( "\n\nlength: %d\n", getSize( presentRTranPtr->blocks ) );

  while( notEnd( presentRTranPtr->blocks ) == TRUE )
    {  //fprintf( stderr, "hi7\n" );
      presentBufBlock = (bufBlock*)getPresentData( presentRTranPtr->blocks );
      free( presentBufBlock );
      move( presentRTranPtr->blocks );
      presentBufCount--;
    }

  ldestroy( presentRTranPtr->blocks );
  ldestroy( presentRTranPtr->toSend );
  ldestroy( presentRTranPtr->toWrite );
  ldestroy( presentRTranPtr->headers );
  ldestroy( presentRTranPtr->entityBody );

  if( presentRTranPtr->cc != NULL )
    {  //fprintf( stderr, "hi6\n" );
      free( presentRTranPtr->cc );
      presentRTranPtr->cc = NULL;
    }

  if( presentRTranPtr->flowEntry == TRUE )
    {  //fprintf( stderr, "hi5\n" );
      if( hfind( fTab, presentRTranPtr->URL, strlen( presentRTranPtr->URL ) ) == FALSE )
	{  //fprintf( stderr, "hi4\n" );
	  fprintf( stderr, "Missing flow entry\n" );
	  return -1;
	}

      hdel( fTab );
    }

  status = 0;

#ifdef USE_DIRTY_CACHE
  if( presentRTranPtr->dob != NULL )
    {
      status = deleteDirtyObject( presentRTranPtr->dob );
      presentRTranPtr->dob = NULL;
    }
#endif

  if( status != 1 )
    {
      free( presentRTranPtr->URL );
      ldestroy( presentRTranPtr->allBlocks );
    }

  presentResCount--;
  free( presentRTranPtr );
  return 0;
}

int delRequestTranPerm( httpRequest *presentReq )
{
  bufBlock *presentBufBlock;

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "C %d\n", presentReq->fd );
#endif

  if( presentReq->fd != -1 && presentReq->method != WAPROX_PUT )
    {
#ifdef USE_EPOLL
      if( epollTran[ presentReq->fd ] != NULL )
	{
	  epollTran[ presentReq->fd ] = NULL;
	  if( epoll_ctl( epfd, EPOLL_CTL_DEL, presentReq->fd, NULL ) < 0 )
	    fprintf( stderr, "Epoll Control Delete failed - %d %d %s\n", presentReq->fd, errno, strerror( errno ) );
	}
#endif
      close( presentReq->fd );
      //presentReq->fd = -1;
    }

  /*#ifdef PERSISTENT_CLIENT
  if( presentReq->fd != -1 )
    {
      close( presentReq->fd );
    }
    #endif*/

  if( presentReq->blocks != NULL )
    {
      startIter( presentReq->blocks );

      while( notEnd( presentReq->blocks ) == TRUE )
	{
	  presentBufBlock = (bufBlock*)getPresentData( presentReq->blocks );
	  free( presentBufBlock );
	  presentBufCount--;
	  move( presentReq->blocks );
	}
    }

  ldestroy( presentReq->blocks );
  ldestroy( presentReq->toSend );
  ldestroy( presentReq->entityBody );
  ldestroy( presentReq->headers );

  presentReq->blocks = NULL;

  if( presentReq->cc != NULL )
    free( presentReq->cc );

  presentReq->cc = NULL;

  if( presentReq->method != WAPROX_PUT )
    {
      readTran[ presentReq->fd ] = NULL;
      writeTran[ presentReq->fd ] = NULL;
      readHandlers[ presentReq->fd ] = NULL;
      writeHandlers[ presentReq->fd ] = NULL;
      usageStamps[ presentReq->fd ].tv_sec = -1;
      deleteHandlers[ presentReq->fd ] = NULL;
      FD_CLR( presentReq->fd, readFDSetPtr );
      FD_CLR( presentReq->fd, writeFDSetPtr );
    }
 
#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "D URL %s\n", presentReq->URL );
#endif

  if( presentReq->info.server != NULL )
    free( presentReq->info.server );

  presentReq->info.server = NULL;

  presentReqCount--;
  free( presentReq );

  return 0;
}

int sendData( httpResponse *presentResPtr )
{
  bufBlock *present;
  int status;
  connection* presentConn;

  if( presentResPtr->method == WAPROX_PUT )
    {
      return 0;
    }

#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "Checking what to send\n" );
#endif

  startIter( presentResPtr->toSend );

  while( notEnd( presentResPtr->toSend ) == TRUE )
    {
      present = (bufBlock*)getPresentData( presentResPtr->toSend );

      if( present->end - present->rstart <= 0 )
	{
	  deletePresent( presentResPtr->toSend );
	  continue;
	}

#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Sending a chunk of size %d\n", present->end - present->rstart );
#endif
      signal( SIGPIPE, SIG_IGN );
      
      if( presentResPtr->presReq == NULL ) {
	presentConn = (connection*)connTran[ presentResPtr->fd ];
	delResponseTranLazy( presentResPtr );
	
	if( presentConn != NULL ) {
	  presentConn->inUse = FALSE;
	  delConnectionLazy( presentConn );
	}

	return 0;
      }

      signal( SIGPIPE, SIG_IGN );

      status = write( presentResPtr->presReq->fd, present->buf + present->rstart, 
			present->end - present->rstart );

      gettimeofday( &usageStamps[ presentResPtr->presReq->fd ], NULL );

      signal( SIGPIPE, SIG_IGN );

      if( status == -1 && errno == EAGAIN )
	{
	  signal( SIGPIPE, SIG_IGN );
	  //	  fprintf( stderr, "Hit EAGAIN\n" );
	  return 2;
	}

      if( status >= 0 && status < present->end - present->rstart )
	{
	  signal( SIGPIPE, SIG_IGN );
	  //	  fprintf( stderr, "No buffer space left in the system\n" ); 
	  present->rstart += status;
	  return 2;
	}

      if( status == -1 )
	{
	  signal( SIGPIPE, SIG_IGN );
	  fprintf( stderr, "Error sending data to client - %s\n", strerror( errno ) );
	  return -1;
	}

      deletePresent( presentResPtr->toSend );
    }

  return 0;
}

int handleConnect( int );

int connectServerPerm( connection *newConn )
{
  int status;
  int sockfd;
  struct sockaddr_in servAddr;
  int disable = 1;
  //  fprintf( stderr, "%s %lu %lu\n", presentEnt->h_addr_list[ 0 ], *(unsigned long*)presentEnt->h_addr_list[ 0 ], inet_addr( "209.85.165.99" ) );  google test

  if( newConn->presentRes == NULL ) {
    delConnectionLazy( newConn );
    return 0;
  }

  sockfd = socket( AF_INET, SOCK_STREAM, 0 );
  
  if( sockfd == -1 )
    {
      fprintf( stderr, "socket call failed: %s\n", strerror( errno ) );
      return -1;
    }

  if( sockfd == 0 )
    {
      fprintf( stderr, "Pid %d %d connects 0\n", getpid(), getppid() );
      //turn -1;
    }

  setsockopt( sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &disable, sizeof(int) );
  long arg = fcntl( sockfd, F_GETFL, NULL );

  if( arg == -1 )
    {
      fprintf( stderr, "Error getting the sock fcntl\n" );
      return -1;
    }

  #if 0
  arg |= O_NONBLOCK;
  status = fcntl( sockfd, F_SETFL, arg );

  if( status == -1 )
    {
      fprintf( stderr, "Error setting non blocking on socket\n" );
      return -1;
    }

  arg = fcntl( sockfd, F_GETFL, NULL );
  
  if( arg == -1 )
    {
      fprintf( stderr, "Error getting the sock fcntl\n" );
      return -1;
    }
  
  arg |= O_NDELAY;
  status = fcntl( sockfd, F_SETFL, arg );
  
  if( status == -1 )
    {
      fprintf( stderr, "Error setting non blocking on socket\n" );
      return -1;
    }
#endif
  status = fcntl( sockfd, F_SETFL, O_NONBLOCK );

  if( status == -1 )
    {
      fprintf( stderr, "Error setting non blocking on socket\n" );
      return -1;
    }
  
#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Connecting to %s on port %d\n", inet_ntoa( *(struct in_addr*)newConn->dnsInfo.presentHost ), newConn->port );
#endif
 
  struct in_addr temp;

  bzero( &servAddr, sizeof( servAddr ) );
  servAddr.sin_family = AF_INET;
  
  if( staticGateway == NULL )
    {
      servAddr.sin_addr.s_addr = *(unsigned long*)newConn->dnsInfo.presentHost;
      servAddr.sin_port = htons( newConn->port );
    }
  else
    {
      inet_aton( staticGateway, &temp );
      servAddr.sin_addr.s_addr = temp.s_addr;
      servAddr.sin_port = htons( gatewayPort );
    }

  if( servAddr.sin_addr.s_addr == INADDR_NONE )
    {
      fprintf( stderr, "Cannot convert the network byte order correctly %s\n",
	       staticGateway );
      return -1;
    }

  status = connect( sockfd, (struct sockaddr*)&servAddr, sizeof( servAddr ) );

  if( status == -1 && errno != EINPROGRESS )
    {
      fprintf( stderr, "Server not recheable\n" );
      return -1;
    }
  
#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "p on %d\n", sockfd );
#endif

  writeTran[ sockfd ] = (char*)newConn;
  writeHandlers[ sockfd ] = &handleConnect;
  newConn->presentRes->connWait = TRUE;

#ifdef USE_WAPROX_HEADER
  if( newConn != NULL && newConn->presentRes != NULL &&
      newConn->presentRes->presReq != NULL )
    newConn->presentRes->presReq->connType = NEWCONN;
#endif

  newConn->fd = sockfd;
  connTran[ newConn->fd ] = (char*)newConn;
  readTran[ sockfd ] = NULL;
  readHandlers[ sockfd ] = NULL;
  gettimeofday( &usageStamps[ sockfd ], NULL );
  FD_CLR( sockfd, readFDSetPtr );
  FD_CLR( sockfd, writeFDSetPtr );

  return 0;
}
	
int sendRequest( httpResponse *presentRTranPtr )
{
  int status;
  httpRequest *presentQTranPtr;
  //  bufBlock *present;
  connection *presentConn = (connection*)connTran[ presentRTranPtr->fd ];

  presentQTranPtr = presentRTranPtr->presReq;

  if( presentConn == NULL )
    {
      return -1;
    }

  if( presentQTranPtr == NULL )
    {
      fprintf( stderr, "Missing request\n" );
      delResponseTranLazy( presentRTranPtr );

      if( presentConn != NULL && presentConn->added == FALSE )
	delConnectionLazy( presentConn );
      else
	if( presentConn != NULL )
	  presentConn->inUse = FALSE;
    
      return -1;
    }

  if( presentQTranPtr->lDel == TRUE )
    {
      presentRTranPtr->type = -1;
      delResponseTranLazy( presentRTranPtr );
      return 0;
    }

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "request on %d\n", presentRTranPtr->fd );
#endif
  
  if( presentRTranPtr->reqHeadSent == FALSE )
    {
      status = sendRequestHeader( presentRTranPtr );
      
      if( status == 2 )
	{
	  return 2;
	}

      if( status == -1 )
	{
	  signal( SIGPIPE, SIG_IGN );
	  fprintf( stderr, "Cannot send data to server\n" );
	  return -1;
	}

      presentRTranPtr->reqHeadSent = TRUE;
    }

  readTran[ presentRTranPtr->fd ] = (char*)presentRTranPtr;
  readHandlers[ presentRTranPtr->fd ] = &handleDownloadRead;

  status = sendRequestData( presentRTranPtr->fd, presentQTranPtr );

  if( status == 0 ) {
    presentQTranPtr->used = 0;
    readTran[ presentQTranPtr->fd ] = (char*)presentQTranPtr;
  }

  if( status == 2 )
    {
      writeTran[ presentRTranPtr->fd ] = (char*)presentRTranPtr;
      writeHandlers[ presentRTranPtr->fd ] = &resumePosting;
      readTran[ presentQTranPtr->fd ] = NULL;
      readHandlers[ presentRTranPtr->fd ] = NULL;
      return 0;
    }
  
  if( status == -1 )
    {
      delConnectionLazy( (connection*)connTran[ presentRTranPtr->fd ] );
      delRequestTranLazy( presentQTranPtr );
      delResponseTranLazy( presentRTranPtr );
      return -1;
    }

  return 0;
}

int checkDNSCacheAndConnect( connection *newConn )
{
  int status;

  status = getDNSInfo( newConn->server, &newConn->dnsInfo );

  if( status != -1 )
    {
      status = updateDNSUsage( newConn->server );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot update DNS usage\n" );
	  return -1;
	}
      
      status = connectServerLazy( newConn );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot connect\n" );
	  return -1;
	}
      
      return 0;
    }
  else
    {
      status = scheduleResolve( newConn );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot schedule a Resolve\n" );
	  return -1;
	}
      
      return 0;
    }

  return 0;
}

int writeData( httpResponse *presentRTranPtr, char *aData, int remaining )
{
  bufBlock *presentBufBlock;
  int size, readable = 0;

  startIter( presentRTranPtr->toWrite );

  while( notEnd( presentRTranPtr->toWrite ) == TRUE )
    {
#ifdef PRINT_PROXY_DEBUG
      //  fprintf( stderr, "." );
#endif
      
      presentBufBlock = (bufBlock*)getPresentData( presentRTranPtr->toWrite );

      size = presentBufBlock->end - presentBufBlock->wstart;

      if( presentBufBlock->del == TRUE && presentBufBlock != presentRTranPtr->prev )
	{
	  if( presentRTranPtr->prev != NULL )
	    {
	      //em--;
	      //free( presentRTranPtr->prev->orig );
	      presentRTranPtr->prev->del = FALSE;
	    }

	  presentRTranPtr->prev = presentBufBlock;
	}

      if( size <= 0 )
	{
	  deletePresent( presentRTranPtr->toWrite );
	  continue;
	}

#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Block of size %d there to write - remaining %d\n", size, remaining ); 
#endif

      if( size <= remaining )
	{
#ifdef PRINT_PROXY_DEBUG
	  //  fprintf( stderr, "/" );
#endif

	  memcpy( aData + readable, presentBufBlock->buf + presentBufBlock->wstart, size );
	  readable += size;
	  remaining -= size;
	  presentRTranPtr->written += size;
	  deletePresent( presentRTranPtr->toWrite );

#ifdef PRINT_PROXY_DEBUG
	  writable -= size;
#endif
	  continue;
	}

      if( remaining == 0 )
	{
	  return readable;
	}

      if( size > remaining )
	{
#ifdef PRINT_PROXY_DEBUG
	  // fprintf( stderr, "\\" );
#endif
	  memcpy( aData + readable, presentBufBlock->buf + presentBufBlock->wstart, remaining );
	  readable += remaining;
	  presentBufBlock->wstart += remaining;
	  presentRTranPtr->written += remaining;

	  remaining = 0;

#ifdef PRINT_PROXY_DEBUG
	  writable -= remaining;
#endif

#ifdef PRINT_PROXY_DEBUG
	  //  fprintf( stderr, "%d\n", readable );
#endif

	  return readable;
	}
    }

  return readable;
}

int updateUsageStamp( int fd )
{
  gettimeofday( &usageStamps[ fd ], NULL );
  return 0;
}

int checkClientExpiry( int fd, stamp now )
{
  int status;
  httpRequest *presentQTranPtr = NULL;
  httpResponse *presentRTranPtr = NULL;
  connection *presentConn = NULL;

  if( readTran[ fd ] == NULL )
    {
      //usageStamps[ fd ].tv_sec = -1;
      return 0;
    }

  if( usageStamps[ fd ].tv_sec != -1 && 
      usageStamps[ fd ].tv_sec + MSL < now.tv_sec )
    {
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "User %d being closed down due to inactivity\n", fd );
#endif

#ifdef PRINT_PROXY_DEBUG
      expireCount++;
#endif

      presentQTranPtr = (httpRequest*)readTran[ fd ];
      presentRTranPtr = (httpResponse*)respTran[ fd ];
      
      if( presentRTranPtr != NULL && presentRTranPtr->fd != -1 )
	{
	  presentConn = (connection*)connTran[ presentRTranPtr->fd ];
	}

      status = delRequestTranLazy( presentQTranPtr );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete request transaction\n" );
	  return -1;
	}

      status = delResponseTranLazy( presentRTranPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete response transaction\n" );
	  return -1;
	}

      delConnectionLazy( presentConn );

      usageStamps[ fd ].tv_sec = -1;
      readTran[ fd ] = NULL;
      readHandlers[ fd ] = NULL;
      deleteHandlers[ fd ] = NULL;
      writeTran[ fd ] = NULL;
      writeHandlers[ fd ] = NULL;
      FD_CLR( fd, readFDSetPtr );
      FD_CLR( fd, writeFDSetPtr );
    }

  return 0;
}

int checkServerAvailability( int fd, stamp now )
{
  int status;

  connection *presentConn;

  if( writeTran[ fd ] == NULL )
    {
      usageStamps[ fd ].tv_sec = -1;
      return 0;
    }

  if( usageStamps[ fd ].tv_sec != -1 && usageStamps[ fd ].tv_sec + MSL < now.tv_sec )
    {
      presentConn = (connection*)writeTran[ fd ];

      status = delConnectionLazy(presentConn );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete connection\n" );
	  return -1;
	}
      
#ifdef PRINT_PROXY_DEBUG
      expireCount++;
#endif

      usageStamps[ fd ].tv_sec = -1;
      writeTran[ fd ] = NULL;
      writeHandlers[ fd ] = NULL;
      deleteHandlers[ fd ] = NULL;
      FD_CLR( fd, readFDSetPtr );
      FD_CLR( fd, writeFDSetPtr );
    }

  return 0;
}

int checkPersistentConnection( int fd, stamp now )
{
  int status;

  connection *presentConn;

  if( readTran[ fd ] == NULL )
    {
      usageStamps[ fd ].tv_sec = -1;
    }

  if( usageStamps[ fd ].tv_sec != -1 && usageStamps[ fd ].tv_sec + MSL < now.tv_sec )
    {
      presentConn = (connection*)connTran[ fd ];

      if( presentConn->presentRes != NULL )
	{
	  presentConn->presentRes->type = CACHE;
	  delResponseTranLazy( presentConn->presentRes );
	}
      
      status = delConnectionLazy( presentConn );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete connection\n" );
	  return -1;
	}

#ifdef PRINT_PROXY_DEBUG
      expireCount++;
#endif

      usageStamps[ fd ].tv_sec = -1;
      readTran[ fd ] = NULL;
      readHandlers[ fd ] = NULL;
      deleteHandlers[ fd ] = NULL;
      FD_CLR( fd, readFDSetPtr );
      FD_CLR( fd, writeFDSetPtr );
    }

  return 0;
}

int delConnectionPerm( connection *presentConn )
{
#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Deleting connection tran on %d\n", presentConn->fd );
#endif

  if( presentConn->fd != -1 )
    {
      //if( presentConn->fd > 2 )
#ifdef USE_EPOLL
      if( epollTran[ presentConn->fd ] != NULL )
	{
	  epollTran[ presentConn->fd ] = NULL;
	  if( epoll_ctl( epfd, EPOLL_CTL_DEL, presentConn->fd, NULL ) < 0 )
	    fprintf( stderr, "Epoll Control Delete failed - %d %d %s\n", presentConn->fd, errno, strerror( errno ) );
	}
#endif

      close( presentConn->fd );
      presentConnCount--;
      FD_CLR( presentConn->fd, readFDSetPtr );
      FD_CLR( presentConn->fd, writeFDSetPtr );
      usageStamps[ presentConn->fd ].tv_sec = -1;
      connTran[ presentConn->fd ] = NULL;
      readTran[ presentConn->fd ] = NULL;
      readHandlers[ presentConn->fd ] = NULL;
      deleteHandlers[ presentConn->fd ] = NULL;
      writeTran[ presentConn->fd ] = NULL;
      writeHandlers[ presentConn->fd ] = NULL;
    }

  if( presentConn->server != NULL )
    free( presentConn->server );

  presentConCount--;
  free( presentConn );

  return 0;
}

int initConnection( connection *presentConn, httpResponse *presentRTranPtr )
{
  httpRequest *presentQTranPtr = presentRTranPtr->presReq;
  presentConn->server = NULL;

  presentConCount++;
  if( presentQTranPtr == NULL || presentQTranPtr->lDel == TRUE || presentQTranPtr->info.server == NULL )
    {
      fprintf( stderr, "Missing client to make a connection\n" );

      if( presentQTranPtr != NULL )
	{
	  delRequestTranLazy( presentQTranPtr );
	}

      delConnectionLazy( presentConn );
      delResponseTranLazy( presentRTranPtr );

      return -1;
    }

  presentConn->fd = -1;
  presentConn->server = malloc( strlen( presentQTranPtr->info.server ) + 1 );
  //  fprintf( stderr, "Server host name %s\n", presentQTranPtr->info.server );
  memcpy( presentConn->server, presentQTranPtr->info.server, strlen( presentQTranPtr->info.server ) + 1 );
  presentConn->port = presentQTranPtr->info.port;
  presentConn->lDel = FALSE;
  presentConn->added = FALSE;
  presentConn->inUse = FALSE;
  presentConn->presentRes = presentRTranPtr;
  presentQTranPtr->dnsInfo = &presentConn->dnsInfo;

  return 0;
}

int formResponseLine( httpResponse *presentRTranPtr )
{
  genBufferLen = 0;

  if( presentRTranPtr->method == WAPROX_GET )
    {
      genBufferLen += sprintf( genBuffer, "HTTP/%d.%d %d %s\r\n", presentRTranPtr->majVersion, presentRTranPtr->minVersion, presentRTranPtr->code, presentRTranPtr->message );
    }
  else
    {
      genBufferLen += sprintf( genBuffer, "HTTP/1.0 %d %s\r\n", presentRTranPtr->code, presentRTranPtr->message );
    }

#ifdef PRINT_PROXY_DEBUG
    fprintf( stderr, "%s\n", genBuffer );
#endif

  return 0;
}
 
int formSendHeader( httpResponse *presentRTranPtr )
{
  stamp now;
  //  char there = FALSE;

  formResponseLine( presentRTranPtr );

  if( presentRTranPtr->method == WAPROX_GET )
    {
      if( presentRTranPtr->con_len >= 0 )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "Content-Length: %d\r\n", presentRTranPtr->con_len );
	}

      genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );

      return 0;
    }
  
  gettimeofday( &now, NULL );

  /*Age*/
  if( presentRTranPtr->age_value > 0 )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, "Age: %ld\r\n", presentRTranPtr->age_value );
    }

#ifndef PERSISTENT_CLIENT
  genBufferLen += sprintf( genBuffer + genBufferLen, "Connection: close\r\n" );
#else
  if( presentRTranPtr->etype == CONN )
    genBufferLen += sprintf( genBuffer + genBufferLen, "Connection: close\r\n" );
  else
    genBufferLen += sprintf( genBuffer + genBufferLen, "Content-Length: %d\r\n", presentRTranPtr->con_len );
#endif

  /*Cache-Control*/
  if( presentRTranPtr->cc != NULL )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, "Cache-Control: " );

      if( presentRTranPtr->cc->private == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "private," );
	}

      if( presentRTranPtr->cc->public == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "public," );
	}

      if( presentRTranPtr->cc->max_age > 0 )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "max-age=%d,", presentRTranPtr->cc->max_age );
	}

      if( presentRTranPtr->cc->s_maxage > 0 )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "s-maxage=%d,", presentRTranPtr->cc->s_maxage );
	}

      if( presentRTranPtr->cc->no_cache == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "no-cache," );
	}

      if( presentRTranPtr->cc->no_store == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "no-store," );
	}

      if( presentRTranPtr->cc->no_transform == PRESENT )
	{	
	  genBufferLen += sprintf( genBuffer + genBufferLen, "no-transform," );
	}

      if( presentRTranPtr->cc->must_revalidate == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "must-revalidate," );
	}

      if( presentRTranPtr->cc->proxy_revalidate == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, "proxy-revalidate," );
	}

      genBufferLen -= 1;
      genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );
    } 

  /*Content-Length*/
  if( presentRTranPtr->method == WAPROX_PUT && presentRTranPtr->con_len >= 0 )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, "Content-Length: %d\r\n", presentRTranPtr->con_len );
    }

  /*Date*/
  int len = 0;
  char *dateRaw;
  char dateAct[ 100 ];

  dateRaw = asctime( gmtime( &now.tv_sec ) );
  
  while( dateRaw[ len++ ] != '\n' );
  
  dateRaw[ len - 1 ] = '\0';
  strncpy( dateAct, dateRaw, len );
  
  genBufferLen += sprintf( genBuffer + genBufferLen, "Date: %s GMT\r\n", dateAct );

  /*Expires*/
  if( presentRTranPtr->expires_value > 0 )
    {
      len = 0;

      dateRaw = asctime( gmtime( &presentRTranPtr->expires_value ) );
      
      while( dateRaw[ len++ ] != '\n' );
      dateRaw[ len - 1 ] = '\0';
      strncpy( dateAct, dateRaw, len );
      
      genBufferLen += sprintf( genBuffer + genBufferLen, "Expires: %s GMT\r\n", dateAct );
    }

  /*Last-Modified*/
  if( presentRTranPtr->last_modified > 0 )
    {
      len = 0;

      dateRaw = asctime( gmtime( &presentRTranPtr->last_modified ) );
      
      while( dateRaw[ len++ ] != '\n' );
      dateRaw[ len - 1 ] = '\0';
      strncpy( dateAct, dateRaw, len );
      
      genBufferLen += sprintf( genBuffer + genBufferLen, "Last-Modified: %s GMT\r\n", dateAct );
    }

  /*Location*/
  if( presentRTranPtr->redir_location != NULL )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, "Location: " );
      genBufferLen += hdrCpy( genBuffer + genBufferLen, presentRTranPtr->redir_location );
      genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );
    }

  /*Transfer-Encoding*/
  if( presentRTranPtr->te != NULL )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, "Transfer-Encoding: chunked\r\n" );
    }

  /*Warning*/
  if( presentRTranPtr->warning != NULL )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, "Warning: " );
      genBufferLen += hdrCpy( genBuffer + genBufferLen, presentRTranPtr->warning );
      genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );
    }

  startIter( presentRTranPtr->headers );

  int x = 0;
  while( notEnd( presentRTranPtr->headers ) == TRUE )
    {
#ifdef PRINT_PROXY_DEBUG
      //      fprintf( stderr, "Some header\n" );
#endif

      x = hdrCpy( genBuffer + genBufferLen, getPresentData( presentRTranPtr->headers ) );

      if( x < 0 )
	{
	  fprintf( stderr, "%s\n", presentRTranPtr->URL );
	  //exit( -1 );
	}
      else
	genBufferLen += x;

      genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );
      move( presentRTranPtr->headers );
    }

  genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );

#ifdef PRINT_PROXY_DEBUG
    fprintf( stderr, "header len %d\n", genBufferLen );
    fprintf( stderr, "%.1000s", genBuffer );
#endif
    
  return 0;
}

int hdrCpy( char *dest, char *src )
{
  int i = 0;
  char c;
  
  //  fprintf( stderr, "Copying %.20s\n", src );

  for(;;)
    {
      while( ( c = src[ i ] ) != CRETURN )
	{
	  dest[ i ] = src[ i ];

	  if( i > 3000 )
	    {
	      fprintf( stderr, "Too much to be copied from %.20s\n", src );
	      fprintf( stderr, "%.100s\n", genBuffer );
	      return -1;
	    }

	  i++;
	}

      if( src[ i ] == CRETURN )
	{
	  if( src[ i + 1 ] == LFEED )
	    {
	      if( src[ i + 2 ] == SPACE || src[ i + 2 ] == TAB )
		{
		  dest[ i ] = src[ i ];
		  i++;
		  dest[ i ] = src[ i ];
		  i++;
		  dest[ i ] = src[ i ];
		  i++;
		}
	      else
		{
		  dest[ i ] = '\0';
		  return i;
		}
	    }
	  else
	    {
	      dest[ i ] = src[ i ];
	      i++;
	    }
	}
    }
}
  
int sendHeader( httpResponse *presentRTranPtr )
{
#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "Sending header\n" );
#endif

  int status;
  formSendHeader( presentRTranPtr );

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "length %d %c %s\n", genBufferLen, presentRTranPtr->type, presentRTranPtr->URL );
#endif

  if( presentRTranPtr->presReq == NULL )
    {
      return 0;
    }

  gettimeofday( &usageStamps[ presentRTranPtr->presReq->fd ], NULL );

  signal( SIGPIPE, SIG_IGN );

  status = write( presentRTranPtr->presReq->fd, genBuffer + presentRTranPtr->headSent, genBufferLen - presentRTranPtr->headSent );

  signal( SIGPIPE, SIG_IGN );

  if( status == -1 && errno == EAGAIN )
    {
      signal( SIGPIPE, SIG_IGN );
      //      fprintf( stderr, "Hit EAGAIN have to reschedule\n" );
      return 2;
    }

  if( status >= 0 && status < genBufferLen - presentRTranPtr->headSent )
    {
      //      fprintf( stderr, "No buffer space left in the system\n" );
      presentRTranPtr->headSent += status;
      return 2;
    }

  if( status == -1 )
    {
      fprintf( stderr, "Cannot send header\n" );
      signal( SIGPIPE, SIG_IGN );
      return -1;
    }

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Header sent to %d\n", presentRTranPtr->presReq->fd );
#endif

  return 0;
}

int formWriteHeader( httpResponse *presentRTranPtr )
{
  formSendHeader( presentRTranPtr );

  genBufferLen -= 2;
  genBufferLen += sprintf( genBuffer + genBufferLen, "Response-Time: %ld\r\n", presentRTranPtr->response_time );

  genBufferLen += sprintf( genBuffer + genBufferLen, "Request-Time: %ld\r\n", presentRTranPtr->request_time );
  
  genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );

  return 0;
}

int checkHeaderLen( httpResponse *presentRTranPtr )
{
  genBufferLen = 0;

  if( presentRTranPtr == NULL )
    {
      return -1;
    }

  formWriteHeader( presentRTranPtr );

  if( genBufferLen > 3000 )
    {
      fprintf( stderr, "Length exceeded %d\n", genBufferLen );
      return -1;
    }

  return 0;
}

int writeHeader( httpResponse *presentRTranPtr, char *data )
{
#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "Writing header\n" );
#endif

  if( checkHeaderLen( presentRTranPtr ) == 0 )
    {
      memcpy( data, genBuffer, genBufferLen );
      return 0;
    }
  else
    {
      return -1;
    }

  //  fprintf( stderr, "%s\n", genBuffer );
  
  return 0;
}

int writeBlockHeader( httpResponse *presentRTranPtr, blockHeader *pHead )
{
  int len = strlen( presentRTranPtr->URL );

  if( len + 1 > URL_SIZE )
    len = URL_SIZE - 1;

  strncpy( pHead->URL, presentRTranPtr->URL, len );
  pHead->URL[ len ] = '\0';
  gettimeofday( &pHead->key, NULL );
  pHead->hash = presentRTranPtr->hash;

  return 0;
}

int formRequestLine( httpRequest *presentQTranPtr )
{
  genBufferLen = 0;

#ifdef USE_WAPROX_HEADER
  /*if( presentQTranPtr->method == WAPROX_PUT ||
    presentQTranPtr->method == WAPROX_GET )*/
  if( presentQTranPtr->connType == NEWCONN )
    genBufferLen += sprintf( genBuffer, "X-WaproxTarget: %s:%d\r\n",
			     inet_ntoa( *(struct in_addr*)presentQTranPtr->dnsInfo->presentHost ),
			     presentQTranPtr->info.port );
  //fprintf( stderr, "%s\n", genBuffer );
#endif

  genBufferLen += sprintf( genBuffer + genBufferLen, 
			   "%s %s HTTP/1.0\r\n", 
			   presentQTranPtr->method == GET?"GET":"POST", 
			   presentQTranPtr->info.absPath?presentQTranPtr->info.absPath:
			   "/" );

  return 0;
}

int formSendRequestHeader( httpRequest *presentQTranPtr, httpResponse *presentRTranPtr )
{
  stamp now;

  gettimeofday( &now, NULL );

  int len = 0;
  char *dateRaw;
  char dateAct[ 100 ];

  formRequestLine( presentQTranPtr );

#ifndef PERSISTENT
  genBufferLen += sprintf( genBuffer + genBufferLen, "Connection: close\r\n" );
#else
  genBufferLen += sprintf( genBuffer + genBufferLen, "Connection: Keep-Alive\r\n" );
#endif

#ifdef NO_ACCEPT_PREFS
  genBufferLen += sprintf( genBuffer + genBufferLen, "Accept: */*\r\n" );
#endif

  if( presentQTranPtr->con_len >= 0 )
    {
      genBufferLen += sprintf( genBuffer + genBufferLen, 
			       "Content-Length: %d\r\n", presentQTranPtr->con_len );
    }

  if( presentQTranPtr->if_mod_since_value > 0 )
    {
      dateRaw = asctime( gmtime( &presentQTranPtr->if_mod_since_value ) );
     
      len = 0;
      while( dateRaw[ len++ ] != '\n' );
      dateRaw[ len - 1 ] = '\0';
      strncpy( dateAct, dateRaw, len );
      dateAct[ len ] = '\r';
      dateAct[ len + 1 ] = '\n';
      dateAct[ len + 2 ] = '\0';
      
      genBufferLen += sprintf( genBuffer + genBufferLen, 
			       "If-Modified-Since: %s\r\n", dateAct );
    }

  if( presentQTranPtr->if_unmod_since_value > 0 )
    {
      dateRaw = asctime( gmtime( &presentQTranPtr->if_unmod_since_value ) );
      
      len = 0;
      while( dateRaw[ len++ ] != '\n' );
      dateRaw[ len - 1 ] = '\0';
      strncpy( dateAct, dateRaw, len );
      dateAct[ len ] = '\r';
      dateAct[ len + 1 ] = '\n';
      dateAct[ len + 2 ] = '\0';
      
      genBufferLen += sprintf( genBuffer + genBufferLen, 
			       "If-Unmodified-Since: %s\r\n", dateAct );
    }

  if( presentRTranPtr->cc != NULL )
    {
      if( presentRTranPtr->cc->must_revalidate == PRESENT || 
	  presentRTranPtr->cc->proxy_revalidate == PRESENT )
	{
	  if( presentRTranPtr->last_modified > 0 )
	    {
	      dateRaw = asctime( gmtime( &presentRTranPtr->last_modified ) );
	      
	      len = 0;
	      while( dateRaw[ len++ ] != '\n' );
	      dateRaw[ len - 1 ] = '\0';
	      strncpy( dateAct, dateRaw, len );
	      dateAct[ len ] = '\r';
	      dateAct[ len + 1 ] = '\n';
	      dateAct[ len + 2 ] = '\0';
	      
	      genBufferLen += sprintf( genBuffer + genBufferLen, 
				       "If-Modified-Since: %s\r\n", dateAct );
	    }
	}
    }

  startIter( presentQTranPtr->headers );

  while( notEnd( presentQTranPtr->headers ) == TRUE )
    {
#ifdef PRINT_PROXY_DEBUG
      //  fprintf( stderr, "Some header\n" );
#endif
      genBufferLen += hdrCpy( genBuffer + genBufferLen, 
			      getPresentData( presentQTranPtr->headers ) );
      genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );
      move( presentQTranPtr->headers );
    }

  if( presentQTranPtr->cc != NULL )
    {
      if( presentQTranPtr->cc->no_cache == PRESENT )
	{
	  genBufferLen += sprintf( genBuffer + genBufferLen, 
				   "Cache-Control: no-cache\r\n" );
	}
    }


  genBufferLen += sprintf( genBuffer + genBufferLen, "\r\n" );

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "%.1000s\n", genBuffer );
#endif
  //  if( presentQTranPtr->cc != NULL && presentQTranPtr->cc->no_cache == PRESENT )
  // {
  //  fprintf( stderr, "%s\n", genBuffer );
      // exit( -1 );
  //  }
  //#endif

  return 0;
}

int sendRequestHeader( httpResponse *presentResponsePtr )
{
  int status;
  httpRequest *presentQTranPtr;

  presentQTranPtr = presentResponsePtr->presReq;

  if( presentQTranPtr == NULL )
    {
      fprintf( stderr, "Missing client\n" );
      delResponseTranLazy( presentResponsePtr );
      return -1;
    }

  formSendRequestHeader( presentQTranPtr, presentResponsePtr );

  signal( SIGPIPE, SIG_IGN );

  status = write( presentResponsePtr->fd, genBuffer + 
		  presentResponsePtr->reqHead, genBufferLen - 
		  presentResponsePtr->reqHead );

  if( status == -1 && errno == EAGAIN )
    {
      signal( SIGPIPE, SIG_IGN );
      //      fprintf( stderr, "No buffer space\n" );
      return 2;
    }

  if( status >= 0 && status < genBufferLen - presentResponsePtr->reqHead )
    {
      presentResponsePtr->reqHead += status;
      //      fprintf( stderr, "No buffer space\n" );
      return 2;
    }

  if( status == -1 )
    {
      signal( SIGPIPE, SIG_IGN );
      fprintf( stderr, "Error: %d - %s\n", errno, strerror( errno ) );
      return -1;
    }

  return 0;
}

int searchAndConnect( httpResponse *presentRTranPtr )
{
  int status;
  connection *presentConn, *newConn;
  httpRequest *presentQTranPtr = presentRTranPtr->presReq;

  if( presentQTranPtr == NULL )
    {
      fprintf( stderr, "Missing client\n" );
      delResponseTranLazy( presentRTranPtr );
      return 0;
    }
  
  char *server = presentQTranPtr->info.server;
  int port = presentQTranPtr->info.port;

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Trying to see if there is a connection to %s on port %d\n" ,server, presentQTranPtr->info.port );
#endif

  presentConn = getConnectionFromBase( server, port );

  if( presentConn != NULL )
    {
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Connection found in base\n" );
#endif

      gettimeofday( &usageStamps[ presentConn->fd ], NULL );
      readTran[ presentConn->fd ] = (char*)presentRTranPtr;
      readHandlers[ presentConn->fd ] = &handleDownloadRead;
      presentRTranPtr->fd = presentConn->fd;

      presentConn->presentRes = presentRTranPtr;
      status = sendRequest( presentRTranPtr );

      if( status == 2 )
	{
	  writeTran[ presentConn->fd ] = (char*)presentConn;
	  writeHandlers[ presentConn->fd ] = &handleConnect;
	  return 0;
	}

      if( status == -1 )
	{
	  readTran[ presentConn->fd ] = NULL;
	  readHandlers[ presentConn->fd ] = NULL;
	  presentRTranPtr->fd = -1;

	  delConnectionLazy( presentConn );

	  status = searchAndConnect( presentRTranPtr );

	  if( status == -1 )
	    {
	      return -1;
	    }
	}

      return 0;
    }

#ifdef PRINT_PROXY_DEBUG
  // fprintf( stderr, "Making a new connection\n" );
#endif

  newConn = (connection*)malloc( sizeof( connection ) );
  status = initConnection( newConn, presentRTranPtr );
  
  if( status == -1 )
    {
      fprintf( stderr, "Client missing\n" );
      free( newConn );
      presentRTranPtr->type = CACHE;
      delResponseTranLazy( presentRTranPtr );
      return -1;
    }
 
  status = checkDNSCacheAndConnect( newConn );
  
  if( status == -1 )
    {
      fprintf( stderr, "Cannot check and connect\n" );
      return -1;
    }
  
  return 0;
}

int sendRequestData( int fd, httpRequest *presentQTranPtr )
{
  int status;
  startIter( presentQTranPtr->toSend );
  bufBlock *present;
  
  while( notEnd( presentQTranPtr->toSend ) == TRUE )
    {
      present = (bufBlock*)getPresentData( presentQTranPtr->toSend );

#ifdef PRINT_PROXY_DEBUG
      //  fprintf( stderr, "%c %d %d\n", (present->buf + present->start)[ 0 ], present->start, present->end );
#endif
      
      status  = write( fd, present->buf + present->rstart, present->end - present->rstart );

      if( status == -1 && errno == EAGAIN )
	{
	  return 2;
	}

      if( status != -1 && status < present->end - present->rstart )
	{
	  present->rstart += status;
	  return 2;
	}

      if( status == -1 )
	{
	  return -1;
	}

      deletePresent( presentQTranPtr->toSend );
    }

#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "Request data sent\n" );
#endif

  return 0;
}

int initHTTPRequest( httpRequest *presentRequest )
{
  if( presentRequest == NULL )
    {
      fprintf( stderr, "Request is NULL\n" );
      return -1;
    }
  
  presentReqCount++;
#ifdef USE_WAPROX_HEADER
  presentRequest->connType = OLDCONN;
#endif

  presentRequest->used = 0;
  presentRequest->status = 0;
  presentRequest->lDel = FALSE;
  presentRequest->rewind = -1;
  presentRequest->start = HEADER;
  presentRequest->state = NONE;
  presentRequest->URL = NULL;
  presentRequest->status = BAD_REQUEST;
  presentRequest->stage = REQUEST_LINE;
  presentRequest->headers = create();
  presentRequest->blocks = create();
  presentRequest->toSend = create();
  presentRequest->con_len = -1;
  presentRequest->te = NULL;
  presentRequest->conn = NULL;
  presentRequest->if_mod_since_value = -1;
  presentRequest->if_unmod_since_value = -1;
  presentRequest->pstage = START;
  presentRequest->etype = 0;
  presentRequest->cc = NULL;
  presentRequest->info.server = NULL;
  presentRequest->fd = 0;

  if( presentRequest->headers == NULL )
    {
      fprintf( stderr, "Cannot create hashtable\n" );
      return -1;
    }

  presentRequest->entityBody = create();

  if( presentRequest->entityBody == NULL )
    {
      fprintf( stderr, "Cannot create list\n" );       
      return -1;
    }
  
  gettimeofday( &presentRequest->key, NULL );
  presentRequest->request_time = presentRequest->key.tv_sec;

  return 0;
}	      

int initHTTPResponse( httpResponse *presentResponse, httpRequest *presentRequest )
{
  if( presentResponse == NULL )
    {
      fprintf( stderr, "Response is NULL\n" );
      return -1;
    }
  
  gettimeofday( &presentResponse->startTime, NULL );

#ifdef USE_DIRTY_CACHE
  presentResponse->dob = NULL;
#endif

  presentResponse->miss = FALSE;
  presentResCount++;
  presentResponse->rcvd = 0;
  presentResponse->entityLen = -1;
  presentResponse->serverHash = getLocation( presentRequest->info.server, strlen( presentRequest->info.server ) );
  presentResponse->contentType = NULL;
  presentResponse->cacheType = HOC;
  presentResponse->batchAdd = FALSE;
  presentResponse->clientIP.s_addr = presentRequest->clientIP.s_addr;
  presentResponse->logType = ERRORLOG;
  presentResponse->version = -1;
  presentResponse->remOffset = 0;
  presentResponse->match = 0;
  presentResponse->flowEntry = FALSE;
  presentResponse->connWait = FALSE;
  presentResponse->prev = NULL;
  presentResponse->reqHeadSent = FALSE;
  presentResponse->reqHead = 0;
  presentResponse->headSent = 0;
  presentResponse->retries = 0;
  presentResponse->used = 0 ;
  presentResponse->request_time = presentResponse->startTime.tv_sec;
  presentResponse->map = FALSE;
  presentResponse->lDel = FALSE;
  presentResponse->len = 0;
  presentResponse->rewind = -1;
  presentResponse->start = HEADER;
  presentResponse->state = NONE;
  presentResponse->status = BAD_RESPONSE;
  presentResponse->headers = create();
  presentResponse->stage = RESPONSE_LINE;
  presentResponse->blocks = create();
  presentResponse->toSend = create();
  presentResponse->toWrite = create();
  presentResponse->allBlocks = create();
  presentResponse->presReq = presentRequest;
  presentResponse->method = presentRequest->method;
  presentResponse->unWritten = 0;
  presentResponse->offset = 0;
  presentResponse->con_len = -1;
  presentResponse->te = NULL;
  presentResponse->conn = NULL;
  presentResponse->hash = presentRequest->hash;
  presentResponse->URL = malloc( strlen( presentRequest->URL ) + 1 );
  memcpy( presentResponse->URL, presentRequest->URL, strlen( presentRequest->URL ) + 1 );
  presentResponse->downloaded = 0;
  presentResponse->type = CACHE;
  presentResponse->pstage = MATCH;
  presentResponse->block = 1;
  presentResponse->sent = 0;
  presentResponse->written = 0;
  respTran[ presentRequest->fd ] = (char*)presentResponse;
  presentResponse->fd = -1;
  presentResponse->sendHead = FALSE;
  presentResponse->cc = NULL;
  presentResponse->cacheCheck = TRUE;
  presentResponse->age_value = -1;
  presentResponse->redir_location = NULL;
  presentResponse->warning = NULL;
  presentResponse->last_modified = -1;
  presentResponse->expires_value = -1;

  if( presentResponse->headers == NULL )
    {
      fprintf( stderr, "Cannot create hashtable\n" );
      return -1;
    }

  presentResponse->entityBody = create();

  if( presentResponse->entityBody == NULL )
    {
      fprintf( stderr, "Cannot create list\n" );       
      return -1;
    }

  return 0;
}	      

int reInitHTTPRequest( httpRequest *presentRequest )
{
  httpResponse *presentRTranPtr;
  bufBlock *presentBufBlock;

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Re init %d\n", presentRequest->fd );
#endif

  if( presentRequest == NULL )
    {
      fprintf( stderr, "Request is NULL\n" );
      return -1;
    }

  presentRTranPtr = (httpResponse*)respTran[ presentRequest->fd ];

  if( presentRTranPtr != NULL )
    {
      if( presentRTranPtr->method != WAPROX_PUT && presentRTranPtr->method != WAPROX_GET )
	{
	  presentRTranPtr->presReq = NULL;
	}

      respTran[ presentRequest->fd ] = NULL;
    }

  if( presentRequest->method != WAPROX_PUT )
    {
      readTran[ presentRequest->fd ] = NULL;
      readHandlers[ presentRequest->fd ] = NULL;
    }

  if( presentRequest->method == GET || presentRequest->method == HEAD )
    {
      readTran[ presentRequest->fd ] = (char*)presentRequest;
      readHandlers[ presentRequest->fd ] = &handleClientRead;
    }

  writeTran[ presentRequest->fd ] = NULL;
  writeHandlers[ presentRequest->fd ] = NULL;
  FD_CLR( presentRequest->fd, readFDSetPtr );
  FD_CLR( presentRequest->fd, writeFDSetPtr );

  if( presentRequest->blocks != NULL )
    {
      startIter( presentRequest->blocks );
      
      while( notEnd( presentRequest->blocks ) == TRUE )
	{
	  presentBufBlock = (bufBlock*)getPresentData( presentRequest->blocks );
	  presentBufCount--;
	  free( presentBufBlock );
	  move( presentRequest->blocks );
	}
    }
 
#ifdef USE_WAPROX_HEADER
  presentRequest->connType = OLDCONN;
#endif

  presentRequest->method = GET;
  presentRequest->prevUsed = 0;
  presentRequest->used = 0;
  presentRequest->status = 0;
  presentRequest->lDel = FALSE;
  presentRequest->rewind = -1;
  presentRequest->start = HEADER;
  presentRequest->state = NONE;
  presentRequest->URL = NULL;
  presentRequest->status = BAD_REQUEST;
  presentRequest->stage = REQUEST_LINE;

  ldestroy( presentRequest->headers );
  presentRequest->headers = create();
  
  ldestroy( presentRequest->blocks );
  presentRequest->blocks = create();

  ldestroy( presentRequest->toSend );
  presentRequest->toSend = create();

  presentRequest->con_len = -1;
  presentRequest->te = NULL;
  presentRequest->conn = NULL;
  presentRequest->if_mod_since_value = -1;
  presentRequest->if_unmod_since_value = -1;
  presentRequest->pstage = START;
  presentRequest->etype = 0;

  if( presentRequest->cc != NULL )
    {
      free( presentRequest->cc );
      presentRequest->cc = NULL;
    }

  if( presentRequest->info.server != NULL )
    {
      free( presentRequest->info.server );
      presentRequest->info.server = NULL;
    }

  if( presentRequest->headers == NULL )
    {
      fprintf( stderr, "Cannot create hashtable\n" );
      return -1;
    }

  ldestroy( presentRequest->entityBody );
  presentRequest->entityBody = create();

  if( presentRequest->entityBody == NULL )
    {
      fprintf( stderr, "Cannot create list\n" );       
      return -1;
    }
  
  gettimeofday( &presentRequest->key, NULL );
  presentRequest->request_time = presentRequest->key.tv_sec;

  return 0;
}

int delRequestTranLazy( httpRequest *presentQTranPtr )
{
  httpResponse *presentRTranPtr;

  if( presentQTranPtr == NULL || presentQTranPtr->lDel == TRUE )
    {
      return 0;
    }
  
#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "L req %d %s\n", presentQTranPtr->fd, presentQTranPtr->URL );
#endif

  if( presentQTranPtr->lDel == FALSE )
    {
      if( presentClientCount == cLimit )
	{
	  readTran[ listenFd ] = malloc( 1 );
	}
      
      presentClientCount--;
    }

  presentRTranPtr = (httpResponse*)respTran[ presentQTranPtr->fd ];

  if( presentRTranPtr != NULL )
    {
      presentRTranPtr->presReq = NULL;
      respTran[ presentQTranPtr->fd ] = NULL;
    }

  presentQTranPtr->lDel = TRUE;

  if( presentQTranPtr->method != WAPROX_PUT )
    {
      readTran[ presentQTranPtr->fd ] = NULL;
      readHandlers[ presentQTranPtr->fd ] = NULL;
    }

  writeTran[ presentQTranPtr->fd ] = NULL;
  writeHandlers[ presentQTranPtr->fd ] = NULL;
  FD_CLR( presentQTranPtr->fd, readFDSetPtr );
  FD_CLR( presentQTranPtr->fd, writeFDSetPtr );
  addToHead( delReqList, 0, (char*)presentQTranPtr );

  return 0;
}

int delResponseTranLazy( httpResponse *presentRTranPtr )
{
  connection *presentConn;

  if( presentRTranPtr == NULL || presentRTranPtr->lDel == TRUE )
    return 0;

#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "L res %d %s\n", presentRTranPtr->fd, presentRTranPtr->URL );
#endif

  presentRTranPtr->lDel = TRUE;

  if( presentRTranPtr->method == WAPROX_PUT && presentRTranPtr->fd != -1 )
    {
      char responseLine[ 50 ];
      presentRTranPtr->tfd = presentRTranPtr->fd;
      sprintf( responseLine, "HTTP/%d.%d 302 OK\r\n", presentRTranPtr->majVersion, presentRTranPtr->minVersion );
      
      if( write( presentRTranPtr->fd, responseLine, 17 ) != 17 ) {
	fprintf( stderr, "Client busy\n" );
      }

#ifdef PERSISTENT_CLIENT
      readTran[ presentRTranPtr->fd ] = (char*)presentRTranPtr->presReq;
      readHandlers[ presentRTranPtr->fd ] = &handleClientRead;
      presentRTranPtr->presReq = NULL;
      presentRTranPtr->used = 0;
#else
      close( presentRTranPtr->fd );
#endif
    }

#ifdef PERSISTENT_CLIENT
  if( presentRTranPtr->method == WAPROX_GET && presentRTranPtr->presReq != NULL )
    {
      readTran[ presentRTranPtr->presReq->fd ] = (char*)presentRTranPtr->presReq;
      readHandlers[ presentRTranPtr->presReq->fd ] = &handleClientRead;
      presentRTranPtr->presReq = NULL;
    }

  if( presentRTranPtr->method != WAPROX_PUT && presentRTranPtr->presReq != NULL )
    {
      readTran[ presentRTranPtr->presReq->fd ] = (char*)presentRTranPtr->presReq;
      readHandlers[ presentRTranPtr->presReq->fd ] = &handleClientRead;
      presentRTranPtr->presReq = NULL;
    }
#endif

  if( presentRTranPtr->presReq != NULL )
    {
      readTran[ presentRTranPtr->presReq->fd ] = NULL;
      respTran[ presentRTranPtr->presReq->fd ] = NULL;
      writeTran[ presentRTranPtr->presReq->fd ] = NULL;
      presentRTranPtr->presReq = NULL;
    }
  
  if( presentRTranPtr->map == 0 && presentRTranPtr->connWait == FALSE && 
      presentRTranPtr->batchAdd == FALSE )
    {
      addToHead( delResList, 0, (char*)presentRTranPtr );
    }

  if( presentRTranPtr->fd != -1 )
    {
#ifndef PERSISTENT_CLIENT
      readTran[ presentRTranPtr->fd ] = NULL;
      readHandlers[ presentRTranPtr->fd ] = NULL;
#else
      if( presentRTranPtr->method != WAPROX_PUT && 
	  presentRTranPtr->method != WAPROX_GET )
	{
	  readTran[ presentRTranPtr->fd ] = NULL;
	  readHandlers[ presentRTranPtr->fd ] = NULL;
	}
#endif

      writeTran[ presentRTranPtr->fd ] = NULL;
      writeHandlers[ presentRTranPtr->fd ] = NULL;
      FD_CLR( presentRTranPtr->fd, readFDSetPtr );
      FD_CLR( presentRTranPtr->fd, writeFDSetPtr );

      presentConn = (connection*)connTran[ presentRTranPtr->fd ];

      if( presentConn != NULL && presentRTranPtr == presentConn->presentRes )
	{
	  presentConn->presentRes = NULL;
	  connTran[ presentRTranPtr->fd ] = NULL;
	}
    }
      
  if( presentRTranPtr->fd != -1 )
    presentRTranPtr->tfd = presentRTranPtr->fd;

  presentRTranPtr->fd = -1;

  return 0;
}

int delConnectionLazy( connection *presentConn )
{
  if( presentConn == NULL || presentConn->lDel == TRUE )
    return 0;

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "L conn on %d\n", presentConn->fd );
#endif

  if( presentConn->added == TRUE )
    delConnectionFromBase( presentConn );

  presentConn->lDel = TRUE;
  presentConn->presentRes = NULL;
  addToHead( delConList, 0, (char*)presentConn );

  if( presentConn->fd != -1 )
    {
      connTran[ presentConn->fd ] = NULL;
      writeTran[ presentConn->fd ] = NULL;
      readTran[ presentConn->fd ] = NULL;
    }

  return 0;
}

int deletePerms()
{
  int status;
  stamp now;

  gettimeofday( &now, NULL );

  startIter( delReqList );

  while( notEnd( delReqList ) == TRUE )
    {
      status = delRequestTranPerm( (httpRequest*)getPresentData( delReqList ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete request transaction\n" );
	  return -1;
	}

      deletePresent( delReqList );
    }

  httpResponse *presentRTranPtr;

  startIter( delResList );

  while( notEnd( delResList ) == TRUE )
    {
      presentRTranPtr = (httpResponse*)getPresentData( delResList );

      if( presentRTranPtr->map == TRUE )
	{
	  move( delResList );
	  continue;
	}
      
      logResponse( presentRTranPtr, &now );

      status = delResponseTranPerm( (httpResponse*)getPresentData( delResList ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete response transaction\n" );
	  return -1;
	}

      deletePresent( delResList );
    }

  startIter( delConList );

  while( notEnd( delConList ) == TRUE )
    {
      status = delConnectionPerm( (connection*)getPresentData( delConList ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete connection\n" );
	  return -1;
	}

      deletePresent( delConList );
    }

   startIter( delReqList );

  while( notEnd( delReqList ) == TRUE )
    {
      status = delRequestTranPerm( (httpRequest*)getPresentData( delReqList ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete request transaction\n" );
	  return -1;
	}

      deletePresent( delReqList );
    }

  startIter( delResList );

  while( notEnd( delResList ) == TRUE )
    {
      presentRTranPtr = (httpResponse*)getPresentData( delResList );

      if( presentRTranPtr->map == TRUE )
	{
	  move( delResList );
	  continue;
	}

      status = delResponseTranPerm( (httpResponse*)getPresentData( delResList ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete response transaction\n" );
	  return -1;
	}

      deletePresent( delResList );
    }

  startIter( delConList );

  while( notEnd( delConList ) == TRUE )
    {
      status = delConnectionPerm( (connection*)getPresentData( delConList ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete connection\n" );
	  return -1;
	}

      deletePresent( delConList );
    }

  return 0;
}

int connectServerLazy( connection *newConn )
{
  addToHead( newConnList, 0, (char*)newConn );
  newConn->presentRes->connWait = TRUE;

  if( newConn->port < 0 )
    {
      fprintf( stderr, "Neg port\n" );
      exit( -1 );
    }

  return 0;
}

int connectAllPendingServers()
{
  int status;

  startIter( newConnList );

  while( notEnd( newConnList ) == TRUE )
    {
      if( presentConnCount == dLimit )
	{
	  canConnect = FALSE;
	  break;
	}

      status = connectServerPerm( (connection*)getPresentData( newConnList ) );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot connect to server\n" );
	  return -1;
	}

      if( status == 1 )
	{
	  return 0;
	}

      presentConnCount++;
      deletePresent( newConnList );
    }

  return 0;
}

int readyForDownload( httpResponse *presentResponse )
{
  bufBlock *presentBufBlock;

  presentResponse->miss = FALSE;
  presentResponse->headSent = 0;
  presentResponse->used = 0;
  presentResponse->match = 0;
  presentResponse->flowEntry = FALSE;
  presentResponse->connWait = FALSE;
  presentResponse->prev = NULL;
  presentResponse->reqHeadSent = FALSE;
  presentResponse->reqHead = 0;
  presentResponse->headSent = 0;
  presentResponse->retries = 0;
  presentResponse->map = FALSE;
  presentResponse->lDel = FALSE;
  presentResponse->len = 0;
  presentResponse->rewind = -1;
  presentResponse->start = HEADER;
  presentResponse->state = NONE;
  presentResponse->status = BAD_RESPONSE;
  
  if( presentResponse->headers == NULL )
    {
      presentResponse->headers = create();
    }

  presentResponse->stage = RESPONSE_LINE;
  presentResponse->unWritten = 0;
  presentResponse->offset = 0;
  presentResponse->con_len = -1;
  presentResponse->te = NULL;
  presentResponse->conn = NULL;
  presentResponse->downloaded = 0;
  presentResponse->type = DOWN;
  presentResponse->pstage = START;
  presentResponse->block = 1;
  presentResponse->sent = 0;
  presentResponse->written = 0;
  
  presentResponse->sendHead = FALSE;
  
  if( presentResponse->cc != NULL )
    {
      free( presentResponse->cc );
      presentResponse->cc = NULL;
    }

  presentResponse->cacheCheck = TRUE;
  presentResponse->age_value = -1;
  presentResponse->redir_location = NULL;
  presentResponse->warning = NULL;
  presentResponse->last_modified = -1;
  
  startIter( presentResponse->headers );

  while( notEnd( presentResponse->headers ) == TRUE )
    {
      deletePresent( presentResponse->headers );
    }

  startIter( presentResponse->blocks );

  while( notEnd( presentResponse->blocks ) == TRUE )
    {
      presentBufBlock = (bufBlock*)getPresentData( presentResponse->blocks );

      free( presentBufBlock );
      deletePresent( presentResponse->blocks );
    }

  startIter( presentResponse->toSend );

  while( notEnd( presentResponse->toSend ) == TRUE )
    {
      deletePresent( presentResponse->toSend );
    }

  startIter( presentResponse->toWrite );

  while( notEnd( presentResponse->toWrite ) == TRUE )
    {
      deletePresent( presentResponse->toWrite );
    }

  presentResponse->block = 1;
  
  int status = searchAndConnect( presentResponse );
  
  if( status == -1 )
    {
      presentResponse->type = CACHE;
      delRequestTranLazy( presentResponse->presReq );
      delResponseTranLazy( presentResponse );
      fprintf( stderr, "Cannot search for conns and connect\n" );
    }
  
  presentResponse->pstage = FIRST;
  return 0;
}

int readyForPut( httpResponse *presentResponse )
{
  int status;

  readTran[ presentResponse->presReq->fd ] = (char*)presentResponse;
  readHandlers[ presentResponse->presReq->fd ] = &handleDownloadRead;
  presentResponse->fd = presentResponse->presReq->fd;
  connTran[ presentResponse->presReq->fd ] = NULL;
  respTran[ presentResponse->presReq->fd ] = (char*)presentResponse;

  presentResponse->majVersion = presentResponse->presReq->majVersion;
  presentResponse->minVersion = presentResponse->presReq->minVersion;
  presentResponse->code = 302;
  presentResponse->message = getOK();
  presentResponse->headSent = 0;
  presentResponse->used = 0;
  presentResponse->match = 0;
  presentResponse->flowEntry = FALSE;
  presentResponse->connWait = FALSE;
  presentResponse->prev = NULL;
  presentResponse->reqHeadSent = TRUE;
  presentResponse->reqHead = 0;
  presentResponse->headSent = 0;
  presentResponse->retries = 0;
  presentResponse->map = FALSE;
  presentResponse->lDel = FALSE;
  presentResponse->len = 0;
  presentResponse->rewind = -1;
  presentResponse->start = BODY;
  presentResponse->state = NONE;

#ifdef PERSISTENT_CLIENT
  presentResponse->etype = CLEN;
#else
  presentResponse->etype = CONN;
#endif

  presentResponse->status = ENTITY_INCOMPLETE;
  presentResponse->stage = ENTITY_FETCH;
  presentResponse->unWritten = 0;
  presentResponse->offset = 0;

  presentResponse->te = NULL;
  presentResponse->conn = NULL;
  presentResponse->downloaded = 0;
  presentResponse->type = DOWN;
  presentResponse->pstage = FIRST;
  presentResponse->block = 1;
  presentResponse->sent = 0;
  presentResponse->written = 0;
  
  presentResponse->sendHead = TRUE;
  presentResponse->cc = NULL;
  presentResponse->cacheCheck = TRUE;
  presentResponse->age_value = -1;
  presentResponse->redir_location = NULL;
  presentResponse->warning = NULL;
  presentResponse->last_modified = -1;
  presentResponse->cacheable = TRUE;

  httpRequest *presentRequest = presentResponse->presReq;
  int used = presentRequest->used;
  int processed = presentRequest->len;
  presentResponse->con_len = presentRequest->con_len;

  //fprintf( stderr, "Content Length: %d\n", presentRequest->con_len );

  status = parseResponse( -1, presentRequest->buf + processed, used - processed, presentResponse );

  if( status == -1 || presentResponse->status == BAD_RESPONSE )
    {
      fprintf( stderr, "Error parsing put message\n" );
      return -1;
    }

  resumeDownload( presentResponse->fd );

  return 0;
}

int sendNegReply( httpResponse *presentRTranPtr )
{
  int status;

  presentRTranPtr->code = 400;
  presentRTranPtr->message = getNF();

  formResponseLine( presentRTranPtr );

  signal( SIGPIPE, SIG_IGN );
  status = write( presentRTranPtr->presReq->fd, genBuffer, genBufferLen );

  if( status < 0 )
    {
      signal( SIGPIPE, SIG_IGN );
      fprintf( stderr, "Waprox client closed\n" );
      return -1;
    }

  return 0;
}

int joinFlow( httpRequest *presentQTranPtr )
{
  int status;
  httpResponse *newRTranPtr;							

#ifdef PRINT_PROXY_DEBUG
  //  fprintf( stderr, "Checking if accepts a cached reply\n" );
#endif

  status = checkIfAcceptsCached( presentQTranPtr );

  if( status == -1 )
    {
#ifdef PRINT_PROXY_DEBUG
      //  fprintf( stderr, "Doesnt accept cached replies\n" );
#endif
      
      /*create new transaction and launch*/
      newRTranPtr = (httpResponse*)malloc( sizeof( httpResponse ) );
      initHTTPResponse( newRTranPtr, presentQTranPtr );

      newRTranPtr->type = DOWN;
      newRTranPtr->pstage = FIRST;

      status = searchAndConnect( newRTranPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot search for server conn and connect\n" );
	  return -1;
	}
      
      return 0;
    }

#ifdef PRINT_PROXY_DEBUG
  // fprintf( stderr, "Parsing conditionals\n" );
#endif
  
  if( presentQTranPtr->if_mod_since_value > 0 && presentQTranPtr->if_unmod_since_value > 0 )
    {
      presentQTranPtr->status = BAD_REQUEST;
      return -1;
    }

  if( presentQTranPtr->if_mod_since_value > 0 )
    {
      newRTranPtr = (httpResponse*)malloc( sizeof( httpResponse ) );
      initHTTPResponse( newRTranPtr, presentQTranPtr );
      newRTranPtr->type = DOWN;
      newRTranPtr->pstage = FIRST;
      newRTranPtr->if_mod_since_value = presentQTranPtr->if_mod_since_value;

      status = searchAndConnect( newRTranPtr );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot search server conns and connect\n" );
	  return -1;
	}
      
      return 0;
    }

   if( presentQTranPtr->if_unmod_since_value > 0 )
    {
      newRTranPtr = (httpResponse*)malloc( sizeof( httpResponse ) );
      initHTTPResponse( newRTranPtr, presentQTranPtr );
      newRTranPtr->type = DOWN;
      newRTranPtr->pstage = FIRST;
      newRTranPtr->if_unmod_since_value = presentQTranPtr->if_unmod_since_value;

      status = searchAndConnect( newRTranPtr );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot check and connect\n" );
	  return -1;
	}
      
      return 0;
    }

#ifdef PRINT_PROXY_DEBUG
   // fprintf( stderr, "Accepts cached replies - checking\n" );
#endif

  newRTranPtr = (httpResponse*)malloc( sizeof( httpResponse ) );

  status = initHTTPResponse( newRTranPtr, presentQTranPtr );
  
  if( status == -1 )
    {
      fprintf( stderr, "Cannot init HTTP response\n" );
      return -1;
    }
  
  if( hfind( fTab, newRTranPtr->URL, strlen( newRTranPtr->URL ) ) == TRUE )
    {
#ifdef PRINT_PROXY_DEBUG
      // fprintf( stderr, "Flow tab entry exists\n" );
#endif

      newRTranPtr->type = WEB;
      newRTranPtr->cacheCheck = FALSE;
      newRTranPtr->cacheable = FALSE;

      status = searchAndConnect( newRTranPtr );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot check and connect\n" );
	  return -1;
	}
      
      return 0;
    }

#ifdef USE_DIRTY_CACHE
  newRTranPtr->dob = getDirtyObject( newRTranPtr->URL );

  if( newRTranPtr->dob != NULL && getNumBlocks( newRTranPtr->dob ) == 0 )
    {
      fprintf( stderr, "Dirty object with no blocks!\n" );
      sendNegReply( newRTranPtr );
      
      exit( -1 );
      fprintf( stderr, "Waprox Miss\n" );
      if( persistentClient( newRTranPtr->presReq ) == TRUE )
	{
	  reInitHTTPRequest( newRTranPtr->presReq );
	}
      else
	{
	  delRequestTranLazy( newRTranPtr->presReq );
	}
      
      delResponseTranLazy( newRTranPtr );
      
      return 1;
    }

  if( newRTranPtr->dob != NULL && getNumBlocks( newRTranPtr->dob ) > 0 )
    {
      newRTranPtr->type = CACHE;
      newRTranPtr->cacheType = DOB;
      newRTranPtr->remOffset = 0;
      newRTranPtr->version = 0;
      newRTranPtr->totalLen = 0;
      newRTranPtr->match = 0;
      processFurtherBlocks( newRTranPtr );
      return 0;
    }
#endif

  setHeader *presentHeaderPtr;
  presentHeaderPtr = getSetHeader( newRTranPtr->hash );
  newRTranPtr->match = -1;

#ifdef PRINT_PROXY_DEBUG
  // fprintf( stderr, "Checking validity\n" );
#endif

  if( presentHeaderPtr != NULL && isValid( presentHeaderPtr ) == 0 )
    {
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "It is valid\n" );
#endif

#if LRU > 0
      status = updateUsage( getSetNum( presentHeaderPtr ) );

      if( status == -1 )
	{
	  return -1;
	}
#endif

      status = getBestMatchByIndex( newRTranPtr->hash, presentHeaderPtr );

      if( status == -1 )
	{
#ifdef PRINT_PROXY_DEBUG
	  fprintf( stderr, "No index entry for this hash\n" );
#endif

	  if( newRTranPtr->method == WAPROX_GET )
	    {
	      sendNegReply( newRTranPtr );

	      if( persistentClient( newRTranPtr->presReq ) == TRUE )
		{
		  reInitHTTPRequest( newRTranPtr->presReq );
		}
	      else
		{
		  delRequestTranLazy( newRTranPtr->presReq );
		}

	      delResponseTranLazy( newRTranPtr );

	      return 1;
	    }

	  status = searchAndConnect( newRTranPtr );

	  if( status == -1 )
	    {
	      fprintf( stderr, "Cannot check and connect\n" );
	      return -1;
	    }

	  return 0;
	}
      //  fprintf( stderr, "Cache hit at %d match %d set\n", status, getSetNum( presentHeaderPtr ) );
    }

  newRTranPtr->type = CACHE;

#ifdef COMPLETE_LOG
  newRTranPtr->cacheType = HOC;
  newRTranPtr->remOffset = getFirstBlockOffset( presentHeaderPtr, status );
  newRTranPtr->version = getFirstBlockVersion( presentHeaderPtr, status );
  newRTranPtr->totalLen = getFirstBlockLen( presentHeaderPtr, status );
  newRTranPtr->match = status;

  if( checkVersion( newRTranPtr->version, newRTranPtr->remOffset ) == -1 )
    {
      newRTranPtr->type = DOWN;
      newRTranPtr->pstage = FIRST;
      
      status = searchAndConnect( newRTranPtr );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot check and connect\n" );
	  return -1;
	}
      
      return 0;
    }

  if( newRTranPtr->totalLen <= 0 || 
      ( checkLogFileLength( newRTranPtr->remOffset ) == FALSE && 
	getRemVersion() == 0 ) )
    {
      newRTranPtr->type = DOWN;
      newRTranPtr->pstage = FIRST;

      if( newRTranPtr->method == WAPROX_GET )
	{
	  sendNegReply( newRTranPtr );
	  
	  if( persistentClient( newRTranPtr->presReq ) )
	    {
	      reInitHTTPRequest( newRTranPtr->presReq );
	    }
	  else
	    {
	      delRequestTranLazy( newRTranPtr->presReq );
	    }
	  
	  delResponseTranLazy( newRTranPtr );
	  
	  return 1;
	}

      status = searchAndConnect( newRTranPtr );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot check and connect\n" );
	  return -1;
	}
      
      return 0;
    }
#endif

#ifdef SETMEM
  //  fprintf( stderr, "Possible hit at match %d\n", status );
  newRTranPtr->remOffset = getSetFromHash( newRTranPtr->hash ) * WAYS + status;
  newRTranPtr->cacheType = HOC;
  newRTranPtr->match = status;
  newRTranPtr->totalLen = 1;
#endif

#ifdef SET
  newRTranPtr->remOffset = getSetFromHash( newRTranPtr->hash ) * WAYS;
  newRTranPtr->cacheType = HOC;
  newRTranPtr->match = -1;
  newRTranPtr->totalLen = WAYS;
#endif

#if COMPLETE_LOG > 0 || SETMEM > 0
  updateClocks( presentHeaderPtr, status );
#endif

  processFurtherBlocks( newRTranPtr );
  return 0;
}

int getBestMatchByIndex( unsigned int hash, setHeader *presentHeaderPtr )
{
  int best = -1;
  unsigned int bestTime = 0;
  int i;
  unsigned int c;

#ifdef PRINT_PROXY_DEBUG
  unsigned int total = 0;
  fprintf( stderr, "searching best match for hash %u set %u\n", hash, 
	   getSetFromHash( hash ) );
#endif

  for( i = 0; i < WAYS; i++ )
    {
      c = getClock( presentHeaderPtr, i );

#ifdef PRINT_PROXY_DEBUG
      total += c;
      fprintf( stderr, "%u ", c );
#endif

      if( bestTime < c && hdrCmp( hash, getIndexHeader( presentHeaderPtr, i ) ) == 0 )
	{
	  bestTime = c;
	  best = i;
	}
    }

#ifdef PRINT_PROXY_DEBUG 
  fprintf( stderr, "\n" );
  if( total != 28 ) {
    for( i = 0; i < WAYS; i++ ) {
      fprintf( stderr,"%u %u\n", getIndexHeader( presentHeaderPtr, i), 
	       getClock( presentHeaderPtr, i ) );
    }
    exit( -1 );
  }
#endif

  return best;
}

int prepareWrite( char *data, httpResponse *presentRTranPtr )
{
  int status;
  blockHeader *pHead = (blockHeader*)data;
  char *aData = data + sizeof( blockHeader );
  blockTailer *pTail = (blockTailer*)(data + FILE_BLOCK_SIZE - sizeof( blockTailer ));
  int match = presentRTranPtr->match;
  int toWrite = 0, toWriteNow = 0, presentUsed = 0, writtenLen = 0;
  presentRTranPtr->used = 0;

  addToTail( presentRTranPtr->allBlocks, 0, data );

  switch( presentRTranPtr->pstage )
    {
    case FIRST:
      status = writeBlockHeader( presentRTranPtr, pHead );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot write block header\n" );
	  presentRTranPtr->type = WEB;
	  removeWriteLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
	  presentRTranPtr->cacheable = FALSE;
	  break;
	}
      
      status = checkHeaderLen( presentRTranPtr );

      if( status == 0 )
	{
	  toWrite = genBufferLen;
	  toWriteNow = min( toWrite, ACTUAL_BLOCK_SIZE );
	  memcpy( aData, genBuffer + writtenLen, toWriteNow );
	  toWrite -= toWriteNow;
	  presentUsed += toWriteNow;
	  writtenLen += toWriteNow;

	  pTail->readable = presentUsed;
	  
	  if( presentRTranPtr->status == ENTITY_COMPLETE && toWrite == 0 && getSize( presentRTranPtr->toWrite ) == 0 )
	    {
	      pTail->status = END;
	      pTail->block = presentRTranPtr->block;
	      pTail->pres = presentRTranPtr->remOffset;
	    }
	  else
	    {
	      pTail->status = NOT_END;
	      pTail->pres = presentRTranPtr->remOffset;
	      pTail->version = getRemVersion();
	      pTail->block = presentRTranPtr->block;
	      presentRTranPtr->remOffset = pTail->next;

#ifdef PRINT_PROXY_DEBUG
	      fprintf( stderr, "Next block at %d\n", pTail->next );
#endif
	    }
            
	  presentRTranPtr->pstage = REM;

	  while( toWrite > 0 )
	    {
	      presentRTranPtr->block++;
	      data = getSmallResponseBuffer();
	      addToTail( presentRTranPtr->allBlocks, 0, data );
	      presentUsed = 0;
	      pHead = (blockHeader*)data;
	      aData = data + sizeof( blockHeader );
	      pTail = (blockTailer*)(data + FILE_BLOCK_SIZE - sizeof( blockTailer ));
	 
	      status = writeBlockHeader( presentRTranPtr, pHead );
	      
	      if( status == -1 )
		{
		  fprintf( stderr, "Cannot write block header\n" );
		  presentRTranPtr->type = WEB;
		  removeWriteLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
		  presentRTranPtr->cacheable = FALSE;
		  break;
		}

	      match = presentRTranPtr->match;
	      toWriteNow = min( toWrite, ACTUAL_BLOCK_SIZE );
	      memcpy( aData, genBuffer + writtenLen, toWriteNow );
	      toWrite -= toWriteNow;
	      presentUsed += toWriteNow;
	      writtenLen += toWriteNow;

	      pTail->readable = presentUsed;

	      if( presentRTranPtr->status == ENTITY_COMPLETE && toWrite == 0 && getSize( presentRTranPtr->toWrite ) == 0 )
		{
		  pTail->status = END;
		  pTail->block = presentRTranPtr->block;
		  pTail->pres = presentRTranPtr->remOffset;
		}
	      else
		{
		  pTail->status = NOT_END;
		  pTail->pres = presentRTranPtr->remOffset;
		  pTail->version = getRemVersion();
		  pTail->block = presentRTranPtr->block;
		  presentRTranPtr->remOffset = pTail->next;

#ifdef PRINT_PROXY_DEBUG
		  fprintf( stderr, "Next block at %d\n", pTail->next );
#endif
		}
            
	      presentRTranPtr->pstage = REM;
	    }
	}

      /*
      status = writeHeader( presentRTranPtr, aData );
      
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Header length %d\n", genBufferLen );
#endif

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot write HTTP header\n" );
	  presentRTranPtr->type = WEB;
	  removeWriteLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
	  presentRTranPtr->cacheable = FALSE;
	  break;
	}
      */

      if( presentUsed == ACTUAL_BLOCK_SIZE )
	{
	  presentRTranPtr->match = match;
	  presentRTranPtr->pstage = REM;
	  break;
	}
      
      status = writeData( presentRTranPtr, aData + presentUsed, ACTUAL_BLOCK_SIZE - presentUsed );
      
      if( status == -1 )
	{
	  delRequestTranLazy( presentRTranPtr->presReq );
	  delResponseTranLazy( presentRTranPtr );
	  fprintf( stderr, "Cannot write data\n" );
	  break;
	}
      
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "%d %d %d %d %d %d %d\n", getSize( delReqList ), getSize( delResList ), getSize( delConList ), (int)rTranTab->logsize, (int)rTranTab->count, (int)asyncTab->logsize, (int)asyncTab->count );
      fprintf( stderr, "Written %d Readable %d\n", status, status + presentUsed );
#endif
      
      pTail->readable = status + presentUsed;

      if( presentRTranPtr->status == ENTITY_COMPLETE && getSize( presentRTranPtr->toWrite ) == 0 )
	{
	  pTail->status = END;
	  pTail->block = presentRTranPtr->block;
	  pTail->pres = presentRTranPtr->remOffset;
	}
      else
	{
	  pTail->status = NOT_END;
	  pTail->pres = presentRTranPtr->remOffset;
	  pTail->version = getRemVersion();
	  pTail->block = presentRTranPtr->block;
	  presentRTranPtr->remOffset = pTail->next;

#ifdef PRINT_PROXY_DEBUG
	  fprintf( stderr, "Next block at %d\n", pTail->next );
#endif
	}
            
      presentRTranPtr->pstage = REM;
      break;
    default:
      match = presentRTranPtr->match;
      
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Using match %d for writing\n", match );
#endif
      
      status = writeBlockHeader( presentRTranPtr, pHead );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot write block header\n" );
	  presentRTranPtr->type = WEB;
	  removeWriteLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
	  presentRTranPtr->cacheable = FALSE;
	  break;
	}
      
      status = writeData( presentRTranPtr, aData, ACTUAL_BLOCK_SIZE );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot write data\n" );
	  return -1;
	}
      
      pTail->readable = status;

#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Written %d Readable %d\n", status, status );
#endif

      if( presentRTranPtr->status == ENTITY_COMPLETE && getSize( presentRTranPtr->toWrite ) == 0 )
	{
	  pTail->status = END;
	  pTail->block = presentRTranPtr->block;
	  pTail->pres = presentRTranPtr->remOffset;
	}
      else
	{
	  pTail->status = NOT_END;
	  pTail->pres = presentRTranPtr->remOffset;
	  pTail->version = getRemVersion();
	  pTail->block = presentRTranPtr->block;
	  presentRTranPtr->remOffset = pTail->next;

#ifdef PRINT_PROXY_DEBUG
	  fprintf( stderr, "Next block at %d\n", pTail->next );
#endif

	}
      
      presentRTranPtr->pstage = REM;
            
      break;
    }

  presentRTranPtr->block++;
  return 0;
}
  
int scheduleResolve( connection *newConn )
{
  int status;
  DNSRequest *presentReqPtr;

  newConn->presentRes->connWait = TRUE;

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "Need to resolve %s\n", newConn->server );
#endif

  if( hfind( dnsTranTab, newConn->server, strlen( newConn->server ) ) == TRUE )
    {
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Entry found - queuing\n" );
#endif

      presentReqPtr = (DNSRequest*)hstuff( dnsTranTab );
      gettimeofday( &newConn->stamp, NULL );
      addToHead( presentReqPtr->connList, 0, (char*)newConn );
    }
  else 
    {
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Entry not found - making new\n" );
#endif

      presentReqPtr = (DNSRequest*)malloc( sizeof( DNSRequest ) );
      presentReqPtr->connList = create();
      presentReqPtr->entry = (char*)presentReqPtr;
      addToHead( presentReqPtr->connList, 0, (char*)newConn );
      strcpy( presentReqPtr->server, newConn->server );
  
#ifdef PRINT_PROXY_DEBUG
      fprintf( stderr, "Scheduling resolve for %s\n", presentReqPtr->server );
#endif

      gettimeofday( &newConn->stamp, NULL );
      memcpy( (char*)(&presentReqPtr->key), (char*)(&newConn->stamp), sizeof( asynckey ) );
      hadd( dnsTranTab, presentReqPtr->server, strlen( presentReqPtr->server ), (char*)presentReqPtr );

      status = write( dnsHelperWrites[ whichDNSHelper() ], (char*)(presentReqPtr), sizeof( DNSRequest ) );
  
      if( status == -1 )
	{
	  fprintf( stderr, "Error scheduling an async write request\n" );
	  return -1;
	}
    }

  return 0;
}

int logResponse( httpResponse *presentResponse, struct timeval *now )
{
  genBufferLen = 0;

  switch( presentResponse->logType )
    {
    case COMMONLOG:
      genBufferLen += sprintf( genBuffer, "%s - hashcache [%f] \"%s %s"\
			       " %d.%d\" %d %d\n", 
			       inet_ntoa( presentResponse->clientIP ), 
			       timeVal( now ), getMethod( presentResponse->method ), 
			       presentResponse->URL, presentResponse->majVersion, 
			       presentResponse->minVersion, presentResponse->code, 
			       presentResponse->downloaded ) ;
      //write( logHelperWrite, genBuffer, genBufferLen );
      break;
    case ERRORLOG:
      genBufferLen += sprintf( genBuffer, "[%f] [error] [client %s] %s\n", timeVal( now ), inet_ntoa( presentResponse->clientIP ), getErrorMessage( presentResponse->logType ) );
      //write( errorHelperWrite, genBuffer, genBufferLen );
      break;
    }

  return 0;
}

int logNow()
{
  if( write( logHelperWrite, genBuffer, genBufferLen ) != genBufferLen ) {
    fprintf( stderr, "Log helper busy\n" );
  }

  return 0;
}

int whichDatHelper()
{
  int i;

  for( i = 0; i < numDataHelpers; i++ )
    {
      if( datHelperStatus[ i ] == 0 )
	{
	  return i;
	}
    }

  presentDataHelper = presentDataHelper + 1;

  if( presentDataHelper == numDataHelpers )
    {
      presentDataHelper = 0;
    }

  return presentDataHelper;
}

int whichDNSHelper()
{
#if USE_DNS_CLONE > 0
  int i;

  for( i = 0; i < numDNSHelpers; i++ )
    {
      if( dnsHelperStatus[ i ] == 0 )
	{
	  return i;
	}
    }
#endif

  presentDNSHelper = presentDNSHelper + 1;

  if( presentDNSHelper >= numDNSHelpers || presentDNSHelper < 0 )
    {
      presentDNSHelper = 0;
    }

  return presentDNSHelper;
}

int persistentClient( httpRequest *presentQTranPtr )
{
#ifndef PERSISTENT_CLIENT
  return FALSE;
#endif

  if( presentQTranPtr == NULL )
    return FALSE;

  if( presentQTranPtr->conn != NULL && 
      ( strncmp( presentQTranPtr->conn, "Close", 5 ) == 0 || 
	strncmp( presentQTranPtr->conn, "close", 5 ) == 0 ) )
    {
      return FALSE;
    }

  return TRUE;
}

int persistentConn( connection *presentConn )
{
#ifndef PERSISTENT
  return FALSE;
#endif
  
  if( presentConn == NULL )
    {
      return FALSE;
    }

  if( presentConn->added == TRUE )
    {
      return TRUE;
    }

  return FALSE;
}

int manageConnection( connection *presentConn )
{
  if( presentConn == NULL )
    return 0;

#ifdef PRINT_PROXY_DEBUG
  fprintf( stderr, "M conn on %d\n", presentConn->fd );
#endif

  presentConn->inUse = FALSE;
  presentConn->presentRes = NULL;

  if( presentConn->fd != -1 )
    {
      writeTran[ presentConn->fd ] = NULL;
      readTran[ presentConn->fd ] = NULL;
    }

  return 0;
}

char* getMethod( int method )
{
  switch( method )
    {
    case GET:
      return "GET";
    case HEAD:
      return "HEAD";
    case POST:
      return "POST";
    case WAPROX_GET:
      return "WAPROX-GET";
    case WAPROX_PUT:
      return "WAPROX-PUT";
    default:
      return "UNKNOWN";
    }

  return (char*)-1;
}

char* getErrorMessage( int logType )
{
  switch( logType )
    {
    case ERRORLOG:
      return "Bad request or bad response";
    }

  return (char*)-1;
}

float timeGap( struct timeval *now, struct timeval *then )
{
  return (float)(now->tv_sec - then->tv_sec) + (float)(now->tv_usec - then->tv_usec)/1000000.0;
}
 
int checkBatchWritesNow()
{
  //  fprintf( stderr, "Checking batch writes now\n" );

  batch* present;
  int oldest;
  struct timeval now;
  mapEntry *presentMapEntry;
  diskRequest* presentDiskRequest = NULL;
  char *block;
  int writeLen;
  char *writeStart;
  int i;
  char *writeBufferOffset;
  httpResponse *presentResponse;
  gettimeofday( &now, NULL );
  int secGap = now.tv_sec - timeSinceProcess.tv_sec;
  int usecGap = now.tv_usec - timeSinceProcess.tv_usec;

  if( secGap > 1 || (secGap > 0 && usecGap >= 0) )
    {
      timeSinceProcess.tv_sec = now.tv_sec;
      timeSinceProcess.tv_usec = now.tv_usec;
      startBatchProcess( &oldest ); 

      while( ( present = getNextEligibleBatch( oldest ) ) != NULL )
	{
#ifdef PRINT_PROXY_DEBUG
	  fprintf( stderr, "Scheduling writes for IP %s\n", 
		   inet_ntoa( *(struct in_addr*)getIP( present ) ) );
#endif
	  processBatch( present );
	}

      if( getUsedBufferCount() > CRITICAL_MASS_BUFFER_SIZE ) 
	processBatch( getLocalhostBatch() );

      printStats();

      processTime();
    }

  formDiskBatches();

  if( ( diskBatchesReady == TRUE || timeGap( &now, &timeSinceWrite ) > ULTIMATE_WRITE_DEADLINE ) && batchWriteLock == 0 )
    {
      if( versionChange == TRUE )
	{
	  fprintf( stderr, "Version Change\n\n\n\n\n\n\n\n\n\n" );

	  for( i = 0; i < numDisks; i++ )
	    {
	      startIter( blockLists[ i ] );

	      while( notEnd( blockLists[ i ] ) == TRUE )
		{
		  returnSmallWriteBuffer( getPresentData( blockLists[ i ] ) );
		  deletePresent( blockLists[ i ] );
		}

	      startIter( presentResponseBatch[ i ] );

	      while( notEnd( presentResponseBatch[ i ] ) == TRUE )
		{
		  presentResponse = (httpResponse*)getPresentData( presentResponseBatch[ i ] );
		  presentResponse->batchAdd = FALSE;
		  presentResponse->map = FALSE;
		  presentResponse->lDel = FALSE;
		  delResponseTranLazy( presentResponse );
		  deletePresent( presentResponseBatch[ i ] );
		}
	    }
	}
      else
	{
	  gettimeofday( &timeSinceWrite, NULL );  

	  writeBufferOffset = writeBuffer;
	  
	  for( i = 0; i < numDisks; i++ )
	    {
#ifdef PRINT_PROXY_DEBUG
	      fprintf( stderr, "%d blocks to be written to disk %d at %d\n", 
		       getSize( blockLists[ i ] ), i, totalBatchOffsets[ i ] );
#endif
	      
	      startIter( blockLists[ i ] );
	      writeStart = writeBufferOffset;
	      writeLen = 0;
	      
	      while( notEnd( blockLists[ i ] ) != FALSE )
		{
		  block = getPresentData( blockLists[ i ] );
		  memcpy( writeBufferOffset, block, FILE_BLOCK_SIZE );
		  returnSmallWriteBuffer( block );
		  deletePresent( blockLists[ i ] );
		  writeBufferOffset += FILE_BLOCK_SIZE;
		  writeLen += FILE_BLOCK_SIZE;
		}
	      
	      if( writeLen > 0 )
		{
		  presentDiskRequest = malloc( sizeof( diskRequest ) );
		  //fprintf( stderr, "alloc %d\n", (int)presentDiskRequest );
		  presentDiskRequest->buffer = writeStart;
		  presentDiskRequest->remOffset = totalBatchOffsets[ i ];
		  //	  fprintf( stderr, "At location %d\n", totalBatchOffsets[ i ] );
		  presentDiskRequest->writeLen = writeLen;
		  presentDiskRequest->type = WRITE;
		  presentDiskRequest->isBatch = TRUE;
		  presentDiskRequest->version = presentBatchVersion;
		  presentDiskRequest->block = 2;
#ifdef USE_MEM_SYS
		  presentMapEntry = (mapEntry*)getChunk( mapEntryMemSys );
#else
		  presentMapEntry = malloc( sizeof( mapEntry ) );
#endif
		  
		  totalMapEntryCount++;
		  presentMapEntry->presentList =  create();

		  startIter( presentResponseBatch[ i ] );
		  while( notEnd( presentResponseBatch[ i ] ) == TRUE )
		    {
		      addToTail( presentMapEntry->presentList, 0, getPresentData( presentResponseBatch[ i ] ) );
		      deletePresent( presentResponseBatch[ i ] );
		    };

		  presentDiskRequest->entry = (char*)presentMapEntry;
		  
		  batchWriteLock += 1;
		  //write( dataHelperWrites[ whichDatHelper() ], (char*)(&presentDiskRequest), sizeof( diskRequest ) );
		  addRequest( diskSchedulers[ whichRem( presentDiskRequest->remOffset ) ], presentDiskRequest );
		}
	    }
	}

      for( i = 0; i < numDisks; i++ )
	{
	  totalBatchOffsets[ i ] = -1;
	}
      
      versionChange = FALSE;
      diskBatchesReady = FALSE;
      totalBlocksUsed = 0;
    }

  return 0;
}

int processBatch( batch* thisBatch )
{
  if( thisBatch == NULL ) 
    return 0;

  list *eList = getResponseList( thisBatch );
  batchElement *eThis;
  httpResponse *presentResponse;
  int x, y;
  list *tempList = create();
  relOb* relatedObjects = NULL;
  int serverHash;

  startIter( eList );
  presentIPBatchCount += getSize( eList );
  presentIPCount++;

  while( notEnd( eList ) == TRUE )
    {
      eThis = getPresentData( eList );
      presentResponse = (httpResponse*)getResponse( eThis );
      x = 0;

      if( presentResponse->type == WEB )
	{
	  presentResponse->batchAdd = FALSE;

	  if( presentResponse->lDel == TRUE )
	    {
	      presentResponse->lDel = FALSE;
	      presentResponse->map = 0;
	      delResponseTranLazy( presentResponse );
	      x = 1;
	    }
	}
      else
	{
	  if( presentResponse->status == BAD_RESPONSE )
	    {
	      presentResponse->batchAdd = FALSE;
	      presentResponse->map = FALSE;
	      presentResponse->lDel = FALSE;
	      
	      startIter( presentResponse->allBlocks );
	      
	      while( notEnd( presentResponse->allBlocks ) == TRUE )
		{
		  returnSmallWriteBuffer( getPresentData( presentResponse->allBlocks ) );
		  deletePresent( presentResponse->allBlocks );
		}
	      
	      delResponseTranLazy( presentResponse );
	      x = 1;
	    }
	  else
	    {
	      if( presentResponse->status == ENTITY_COMPLETE )
		{
		  startIter( tempList );
		  y = 0;
		  x = 1;
		  
		  while( notEnd( tempList ) == TRUE )
		    {
		      serverHash = getPresentValue( tempList );
		      
		      if( serverHash == presentResponse->serverHash )
			{
			  relatedObjects = getPresentData( tempList );
			  addToRelOb( relatedObjects, presentResponse );
			  
			  y = 1;
			  break;
			}
		      
		      if( serverHash > presentResponse->serverHash )
			{
			  relatedObjects = malloc( sizeof( relOb ) );
			  relatedObjects->size = 0;
			  relatedObjects->urls = create();
			  addToRelOb( relatedObjects, presentResponse );		            
			  addSorted( tempList, relatedObjects, presentResponse->serverHash );
			  
			  y = 1;
			  break;
			}
		      
		      move( tempList );
		    }
		  
		  if( y == 0 )
		    {
		      relatedObjects = malloc( sizeof( relOb ) );
		      relatedObjects->size = 0;
		      relatedObjects->urls = create();
		      addToRelOb( relatedObjects, presentResponse );
		      addSorted( tempList, relatedObjects, presentResponse->serverHash );
		    }
		}
	      else
		{
		  if( presentResponse->status != BUFFER_WAIT )
		    {
		      if( presentResponse->con_len < 0 && 
			  getSize( presentResponse->allBlocks ) > CONN_LESS_LIMIT )
			{
			  startIter( presentResponse->allBlocks );
			  
			  while( notEnd( presentResponse->allBlocks ) == TRUE )
			    {
			      returnSmallWriteBuffer( getPresentData( presentResponse->allBlocks ) );
			      deletePresent( presentResponse->allBlocks );
			    }
			  
			  presentResponse->batchAdd = FALSE;
			  presentResponse->map = FALSE;
			  presentResponse->type = WEB;
			  presentResponse->cacheable = FALSE;
			  x = 1;
			}
		    }
		}
	    }
	}
 
      if( x == 1 )
	{
	  deletePresent( eList );
	}
      else
	{
	  move( eList );
	}
    }

  startIter( tempList );
  //  fprintf( stderr, "%d pages loaded\n", getSize( tempList ) );
 
  while( notEnd( tempList ) == TRUE )
    {
      addToTail( globalWriteWaitList, 0, getPresentData( tempList ) );
      deletePresent( tempList );
    }
  
  if( getSize( eList ) > 0 )
    addBack( thisBatch );

  ldestroy( tempList );
  return 0;
}

int addToRelOb( relOb *relatedObjects, httpResponse *presentResponse )
{
#ifdef COMPLETE_LOG
  relatedObjects->size += getSize( presentResponse->allBlocks );
#else
  relatedObjects->size += getSize( presentResponse->allBlocks ) - 1;
#endif

  if( strcasestr( presentResponse->URL, "htm" ) != NULL || 
      (presentResponse->contentType != NULL && 
       strcasestr( presentResponse->contentType, "htm" ) != NULL ) )
    {
      addSorted( relatedObjects->urls, presentResponse, -1 );
    }
  else
    {
      if( presentResponse->con_len < 0 )
	{
	  addSorted( relatedObjects->urls, presentResponse, MAX_LENGTH );
	}
      else
	{
	  addSorted( relatedObjects->urls, presentResponse, presentResponse->con_len );
	}
    }

  return 0;
}

int formDiskBatches()
{
  relOb *thisBatch, *newBatch;
  int thisStart, thisVersion, diskNo, leftOnDisk, thisTotal, count, thisUsed;
  httpResponse *presentResponse;
  blockTailer *pTail;
  blockHeader *pHead;
  char *block;
  int status;
#ifndef COMPLETE_LOG
  diskRequest* presentDiskRequest;
  mapEntry* presentMapEntry;
#endif

#ifdef USE_DIRTY_CACHE
  list *newList = NULL;
#endif

  startIter( globalWriteWaitList );
  
  while( notEnd( globalWriteWaitList ) == TRUE )
    {
      thisBatch = getPresentData( globalWriteWaitList );

      if( thisBatch->size > writeBufferSize ) do {
	  newBatch = malloc( sizeof( relOb ) );
	  newBatch->urls = create();
	  newBatch->size = 0;
	  startIter( thisBatch->urls );
	  while( notEnd( thisBatch->urls ) == TRUE &&
		 newBatch->size < MAX_BATCH_SIZE ) {
	    presentResponse = getPresentData( thisBatch->urls );
	    deletePresent( thisBatch->urls );
#ifdef COMPLETE_LOG
	    thisBatch->size -= getSize( presentResponse->allBlocks );
	    addToTail( newBatch->urls, 0, presentResponse );
	    newBatch->size += getSize( presentResponse->allBlocks );
#endif
	  }
	  addToTail( globalWriteWaitList, 0, newBatch );
	}while( thisBatch->size > MAX_BATCH_SIZE );

      if( totalBlocksUsed + thisBatch->size > writeBufferSize )
	{
	  /*	  if( batchWriteLock > 0 ) {
	    fprintf( stderr, "Writes ready but disks locked\n" );
	    }*/

	  diskBatchesReady = TRUE;
	  break;
	}

      beginAllocCycle();

      getFromOneDisk( thisBatch->size, &thisStart, &thisVersion, 
		      &diskNo, &leftOnDisk ); 

      totalBlocksUsed += thisBatch->size;
      thisUsed = 0;
      if( totalBatchOffsets[ diskNo ] == -1 )
	{
	  totalBatchOffsets[ diskNo ] = thisStart;
	  presentBatchVersion = thisVersion;
	}

      if( presentBatchVersion != thisVersion )
	{
	  versionChange = TRUE;
	}

      startIter( thisBatch->urls );
      presentBatchTotal += thisBatch->size;
      presentCacheCount++;
      presentBatchCount += getSize( thisBatch->urls );

      while( notEnd( thisBatch->urls ) == TRUE )
	{
	  presentResponse = getPresentData( thisBatch->urls );
	  thisTotal = getSize( presentResponse->allBlocks );

	  count = 0;

#ifdef USE_DIRTY_CACHE
	  if( presentResponse->dob != NULL && ifUsed( presentResponse->dob ) == TRUE )
	    {
	      //	      fprintf( stderr, "Dirty object needs repair\n" );
	      newList = create();

#ifdef COMPLETE_LOG
	      getKBlocks( newList, getSize( presentResponse->allBlocks), thisStart );
#else
	      getKBlocks( newList, getSize( presentResponse->allBlocks ) - 1,
			  getOffsetFromHash( presentResponse->hash, 
					     presentResponse->match ),
			  thisStart );
#endif

	      startIter( newList );
	    }
#endif

	  startIter( presentResponse->allBlocks );
	  //fprintf( stderr, "%s %d\n", presentResponse->URL, thisTotal );

	  while( notEnd( presentResponse->allBlocks ) != FALSE )
	    {
	      block = getPresentData( presentResponse->allBlocks );

	      if( count == 0 )
		{
		  status = checkForFreeBlocks( presentResponse );

		  if( status == -1 )
		    {
		      fprintf( stderr, "Cannot check for free blocks\n" );
		      //exit( -1 );
		    }

		  if( status == 1 )
		    {
		      presentResponse->type = WEB;
		    }

		  if( status == 0 )
		    {
#ifdef PRINT_PROXY_DEBUG
		      fprintf( stderr, "hash %u setnum %u match %d\n",
			       presentResponse->hash, 
			       getSetFromHash( presentResponse->hash ), 
			       presentResponse->match ); 
#endif

#ifdef COMPLETE_LOG
		      updateHeaders( presentResponse->hash, 
				     presentResponse->match, 
				     presentResponse->hash, thisStart, 
				     determineSmartLength( thisTotal, 
							   thisBatch->size - 
							   thisUsed, leftOnDisk ), 
				     thisVersion );
#else
		      updateHeaders( presentResponse->hash,
				     presentResponse->match,
				     presentResponse->hash );
#endif
		    }

		  totalCacheCount++;
		}

	      pHead = (blockHeader*)block;
	      pTail = (blockTailer*)(block + FILE_BLOCK_SIZE - sizeof( blockTailer ) );

#ifdef COMPLETE_LOG	      
	      pTail->pres = thisStart;
	      pTail->version = thisVersion;
	      pTail->next = thisStart + numDisks;
	      pTail->block = count++;
	      pTail->length = determineSmartLength( thisTotal, thisBatch->size - 
						    thisUsed, leftOnDisk );
	      thisStart += numDisks;
	      thisUsed++;

	      addToTail( blockLists[ diskNo ], 0, block );
#else
	      if( count == 0 ) {
		pTail->version = thisVersion;
		pTail->next = thisStart;
		pTail->block = count++;
		pTail->length = determineSmartLength( thisTotal, thisBatch->size - 
						      thisUsed, leftOnDisk );
		
		pTail->pres = getSetFromHash( presentResponse->hash ) * WAYS +
		  presentResponse->match;
		presentResponse->map++;
		presentDiskRequest = malloc( sizeof( diskRequest ) );
		presentDiskRequest->buffer = block;
		presentDiskRequest->writeLen = FILE_BLOCK_SIZE;
		presentDiskRequest->type = WRITE;
		presentDiskRequest->isBatch = FALSE;
		presentDiskRequest->match = presentResponse->match;
		presentDiskRequest->hash = presentResponse->hash;
		presentDiskRequest->block = 1;

#ifdef USE_MEM_SYS
		presentMapEntry = (mapEntry*)getChunk( mapEntryMemSys );
#else
		presentMapEntry = malloc( sizeof( mapEntry ) );
#endif
		  
		totalMapEntryCount++;
		presentMapEntry->presentList = create();
		addToHead( presentMapEntry->presentList, 0, presentResponse );
		presentDiskRequest->entry = (char*)presentMapEntry;
		
		//write( dataHelperWrites[ whichDatHelper() ], (char*)(&presentDiskRequest), sizeof( diskRequest ) );
		addRequest( diskSchedulers[ whichRem( presentDiskRequest->remOffset ) ], 
			    presentDiskRequest );

	      } else {
		pTail->pres = thisStart;
		pTail->version = thisVersion;
		pTail->next = thisStart + numDisks;
		pTail->block = count++;
		pTail->length = determineSmartLength( thisTotal, thisBatch->size - 
						      thisUsed, leftOnDisk );
		thisStart += numDisks;
		thisUsed++;

		addToTail( blockLists[ diskNo ], 0, block );
	      }
#endif

#ifdef USE_DIRTY_CACHE
	      if( newList != NULL )
		{
		  memcpy( getPresentData( newList ), block, FILE_BLOCK_SIZE );
		  move( newList );
		}
#endif

	      presentResponse->map--;
	      move( presentResponse->allBlocks );
	    }

#ifdef USE_DIRTY_CACHE	  
	  if( newList != NULL )
	    {
	      reAssignBlocks( presentResponse->dob, newList );
	      //	      deleteDirtyObject( presentResponse->dob );
	      //	      presentResponse->dob = NULL;
	      ldestroy( newList );
	      newList = NULL;
	    }
	  else
	    {
	      deleteDirtyObject( presentResponse->dob );
	      presentResponse->dob = NULL;
	    }
#endif

#ifndef COMPLETE_LOG
	  if( thisTotal > 1 ) {
#endif
	    presentResponse->map += 1;
	    addToHead( presentResponseBatch[ diskNo ], 0, (char*)presentResponse );
#ifndef COMPLETE_LOG
	  }
#endif
	  deletePresent( thisBatch->urls );
	}

      ldestroy( thisBatch->urls );
      free( thisBatch );
      deletePresent( globalWriteWaitList );
    }

  return 0;
}

inline int determineSmartLength( int actLength, int remResponses, int leftOnDisk )
{
  return remResponses;
}

inline void printStats()
{
  int i;
  fprintf( stderr, "\n\n" );
  fprintf( stderr, "%lu %d %d %d\n", (unsigned long)timeSinceProcess.tv_sec, 
	   getRemVersion(), getRemOffsetDummy(), totalCacheCount );
  fprintf( stderr, "Num locks: %d\n", lockCount() );
  fprintf( stderr, "HOC locks: %d %d\n", getReadLockCount(), getWriteLockCount() );
  printSetStats();
  printRequestCount();
  printBufferStats();
  printHOCStats();
  printDNSStats();
  fprintf( stderr, "Num dirty: %d\n", getNumDirty() );
  printListStats();
  printHashtableStats();
  printLogFileLengths();
  fprintf( stderr, "Num web-objects ready to be flushed: %d\n", 
	   getSize( globalWriteWaitList ) ); 
  for( i = 0; i < numDisks; i++ )
    {
      printEleStats( diskSchedulers[ i ] );
    }
  
  fprintf( stderr, "Requests: %d Responses: %d Connections: %d HTTP Buffers:"\
	   " %d\n", presentReqCount, presentResCount, presentConCount, 
	   presentBufCount );
  fprintf( stderr, "\n\n" );
}
