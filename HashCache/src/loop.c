#include "loop.h"

fd_set readFDSet, writeFDSet, errorFDSet;
fd_set *readFDSetPtr, *writeFDSetPtr;
static int highestFd;
static char acceptsNeeded;
static int inSelect;
struct timeval timeSinceWrite;
struct timeval timeSinceCheck;
struct timeval timeSinceIndex;
int totalCacheCount;
int presentRequestCount;
int prevRequestCount;
int presentFalsePositive;
int prevFalsePositive;
int presentBytes;
int presentReqCount;
int presentResCount;
int presentConCount;
int presentBufCount;
int presentBatchTotal;
int presentBatchCount;
int presentCacheCount;
int presentIPBatchCount;
int presentIPCount;

struct timeval loopStart, loopEnd, now;

static char *data;

#ifdef PRINT_TIME_DEBUG
int writable, expireCount;
static int total, cacheable, totalGet, badCount;
static stamp now1, now2;
#endif

int loop()
{
  int numReady;
  int (*handler)(int);
  int i, status;
  useconds_t n = COME_AGAIN_TIME;
  totalCacheCount = 0;
  presentRequestCount = 0;
  prevRequestCount = 0;
  presentFalsePositive = 0;
  prevFalsePositive = 0;
  presentBytes = 0;
  presentConCount = 0;
  presentReqCount = 0;
  presentResCount = 0;
  presentBufCount = 0;
  presentBatchTotal = 0;
  presentCacheCount = 0;
  presentIPBatchCount = 0;
  presentIPCount = 0;
  //stamp now;

#ifdef PRINT_LOOP_DEBUG
  total = 0;
  cacheable = 0;
  writable = 0;
  totalGet = 0;
  badCount = 0;
#endif

#ifdef PRINT_TIME_DEBUG
  float timeTaken;
#endif

#ifdef USE_EPOLL
  int presentFD;
#endif

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Starting alarm sequence\n" );
#endif

  readFDSetPtr = &readFDSet;
  writeFDSetPtr = &writeFDSet;

  if( write( timerHelperWrite, (char*)&n, sizeof( useconds_t ) ) != sizeof( useconds_t ) ) {
    fprintf( stderr, "Timer helper is busy\n" );
  }

  if( write( indexHelperWrite, (char*)&n, sizeof( useconds_t ) ) != sizeof( useconds_t ) ) {
    fprintf( stderr, "Index helper is busy\n" );
  }

  gettimeofday( &timeSinceWrite, NULL );
  gettimeofday( &timeSinceCheck, NULL );
  gettimeofday( &timeSinceIndex, NULL );

  signal( SIGPIPE, SIG_IGN );
  signal( SIGCHLD, &sigChildHandler );

  //   int j;
  // for( j = 0; j < 100; j++ )
  for(;;)
    {
#ifdef PRINT_LOOP_DEBUG
      //      fprintf( stderr, "Making fd sets\n" );
#endif
      signal( SIGPIPE, SIG_IGN );

      status = makeFDSets();

      if( status == -1 )
	{
	  fprintf( stderr, "Error making fd sets\n" );
	  return -1;
	}

#ifdef PRINT_LOOP_DEBUG
      //fprintf( stderr, "In to select/epoll, highest fd = %d, clients = %d, servers = %d, selected = %d, locks = %d\n", highestFd, presentClientCount, presentConnCount, inSelect, lockCount() );
#endif
#ifndef USE_EPOLL
      numReady = select( highestFd + 1, (fd_set*)&readFDSet, (fd_set*)&writeFDSet, NULL, NULL );

      //fprintf( stderr, "%d %d %d %d %d %d %d\n", getSize( delReqList ), getSize( delResList ), getSize( delConList ), (int)rTranTab->logsize, (int)rTranTab->count, (int)asyncTab->logsize, (int)asyncTab->count );

#ifdef PRINT_TIME_DEBUG
      gettimeofday( &loopStart, NULL );
#endif

      if( numReady == -1 )
	{
	  fprintf( stderr, "Error selecting, errno = %d\n%s\n", errno, strerror( errno ) );
	  return -1;;
	}

#ifdef PRINT_LOOP_DEBUG
      // fprintf( stderr, "Out of select with %d\n", numReady );
#endif

      for( i = 0; i < highestFd + 1; i++ )
	{
	  if( FD_ISSET( i, &errorFDSet ) )
	    {
	      fprintf( stderr, "Error on %d\n", i );
	      return -1;
	    }
	}

      if( FD_ISSET( listenFd, &readFDSet ) )
	{
	  acceptsNeeded = TRUE;
#ifndef USE_EPOLL
	  FD_CLR( listenFd, &readFDSet );
#endif
	}

      for( i = 0; i < highestFd + 1; i++ )
	{
	  if( FD_ISSET( i, &readFDSet ) )
	    {
#ifdef PRINT_LOOP_DEBUG
	      //      fprintf( stderr, "fd %d ready for read\n", i );
#endif

	      handler = readHandlers[ i ];

	      if( handler == NULL )
		{
		  fprintf( stderr, "Read handler for fd %d is NULL!\n", i );
		  continue;
		}
	      
	      status = (*handler)( i );
	      
	      if( status == -1 )
		{
		  fprintf( stderr, "Error processing fds\n" );
		  //		  return -1;
		  continue;
		}
	      
#ifdef PRINT_TIME_DEBUG
	      gettimeofday( &now2, NULL );
	      //	      fprintf( stderr, "%f\n", (now2.tv_sec - now1.tv_sec)*1000+ ((float)(now2.tv_usec - now1.tv_usec)/1000 ));
#endif
	    }
	      
	  if( FD_ISSET( i, &writeFDSet ) )
	    {
#ifdef PRINT_LOOP_DEBUG
	      fprintf( stderr, "fd %d ready for write\n", i );
#endif
	      
	      handler = writeHandlers[ i ];

	      if( handler == NULL )
		{
		  fprintf( stderr, "Write handler for fd %d is NULL!\n", i );
		  return -1;
		}
	      
	      status = (*handler)( i );
	      
	      if( status == -1 )
		{
		  fprintf( stderr, "Error processing fds\n" );
		  continue;
		}

#ifdef PRINT_TIME_DEBUG
	      gettimeofday( &now2, NULL );
	      //	      fprintf( stderr, "%f\n", (now2.tv_sec - now1.tv_sec)*1000+ ((float)(now2.tv_usec - now1.tv_usec)/1000 ));
#endif
	    }
	}
#else
      numReady = epoll_wait( epfd, epollEventsOut, MY_FD_SETSIZE, -1 );
      
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Out of epoll with %d\n", numReady );
#endif

      if( numReady < 0 )
	{
	  fprintf( stderr, "Epoll failed - %s\n", strerror( errno ) );

	  if( errno != EINTR )
	    exit( -1 );
	}

      for( i = 0; i < numReady; i++ )
	{
	  presentFD = epollEventsOut[ i ].data.fd;

#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "fd %d ready for operation\n", presentFD );
#endif	  

	  if( epollEventsOut[ i ].data.fd == listenFd )
	    {
	      acceptsNeeded = TRUE;
	      continue;
	    }
	  
	  if( ( epollEventsOut[ i ].events & EPOLLIN ) > 0 )
	    {
	      handler = readHandlers[ presentFD ];

	      if( handler == NULL )
		{
		  fprintf( stderr, "Read handler for fd %d is NULL!\n", presentFD );
		  continue;
		}
	      
	      status = (*handler)( presentFD );
	      
	      if( status == -1 )
		{
		  fprintf( stderr, "Error processing fds\n" );
		  continue;
		}
	    }
	  
	  if( (epollEventsOut[ i ].events & EPOLLOUT ) > 0 )
	    {
	      handler = writeHandlers[ presentFD ];
	      
	      if( handler == NULL )
		{
		  fprintf( stderr, "Write handler for fd %d is NULL!\n", presentFD );
		  continue;
		}
	      
	      status = (*handler)( presentFD );
	      
	      if( status == -1 )
		{
		  fprintf( stderr, "Error processing fds\n" );
		  continue;
		}
	    }
	      
#ifdef PRINT_TIME_DEBUG
	  gettimeofday( &now2, NULL );
	  //	      fprintf( stderr, "%f\n", (now2.tv_sec - now1.tv_sec)*1000+ ((float)(now2.tv_usec - now1.tv_usec)/1000 ));
#endif
	}
#endif

      status = deletePerms();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete permanently\n" );
	  return -1;
	}
      
#ifdef PRINT_TIME_DEBUG
      gettimeofday( &now2, NULL );
      //fprintf( stderr, "%f ", (now2.tv_sec - now1.tv_sec)*1000+ ((float)(now2.tv_usec - now1.tv_usec))/1000 );
#endif
      status = connectAllPendingServers();

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot connect to pending servers\n" );
	  return 0;
	}
      
#ifdef PRINT_TIME_DEBUG
      gettimeofday( &now2, NULL );
      //  fprintf( stderr, "%ld ", (now2.tv_sec - now1.tv_sec)*1000+ (now2.tv_usec - now1.tv_usec)/1000 );
#endif

      if( acceptsNeeded == TRUE )
	{
	  acceptsNeeded = FALSE;

	  status = handleAccept( listenFd );

	  if( status == -1 )
	    {
	      fprintf( stderr, "Cannot accept new connections\n" );
	      return -1;
	    }
	}

      checkBatchWritesNow();
      closeOldConnections();
      
      gettimeofday( &now, NULL );

      if( now.tv_sec - timeSinceIndex.tv_sec > INDEX_WRITE_DEADLINE ) {
	if( write( indexHelperWrite, &n, sizeof( int ) ) != sizeof( int ) ) {
	  fprintf( stderr, "Index helper is busy\n" );
	}

	memcpy( &timeSinceIndex, &now, sizeof( struct timeval ) );
      }

#ifdef PRINT_TIME_DEBUG
      gettimeofday( &loopEnd, NULL );
      timeTaken = (float)(loopEnd.tv_sec - loopStart.tv_sec)*1000+ ((float)(loopEnd.tv_usec - loopStart.tv_usec))/1000;

      if( timeTaken > 15.0 )
	{
	  printf( "too much loop time %f\n", timeTaken );
	}
#endif

    }

  return 0;
}

int makeFDSets()
{
  int i;

#ifndef USE_EPOLL
  FD_ZERO( &readFDSet );
  FD_ZERO( &writeFDSet );
  FD_ZERO( &errorFDSet );
  inSelect = 0;

  for( i = 0; i < MY_FD_SETSIZE; i++ )
    {
      if( readTran[ i ] != NULL )
	{
	  FD_SET( i, &readFDSet );
	  //	  MY_FD_SET( i, &errorFDSet );
	  inSelect++;

	  /*
	  if( (int)readHandlers[ i ] == (int)&handleClientRead )
	    fprintf( stderr, "client %d for read\n", i ); 
	  */
	  highestFd = i;
	}

      if( writeTran[ i ] != NULL )
	{
	  FD_SET( i, &writeFDSet );
	  //	  MY_FD_SET( i, &errorFDSet );
	  inSelect++;
	  // fprintf( stderr, "%d for write\n", i );
 
	  highestFd = i;
	}
    }
#else
  int status;

  for( i = 0; i < MY_FD_SETSIZE; i++ )
    {
      if( epollTran[ i ] != NULL )
	{
	  if( epoll_ctl( epfd, EPOLL_CTL_DEL, i, NULL ) < 0 )
	    fprintf( stderr, "Epoll Control Delete failed - %d %d %s\n", i, errno, strerror( errno ) );
	}

      epollEventsIn[ i ].events = 0;
      epollEventsIn[ i ].data.fd = i;

      if( readTran[ i ] != NULL )
	{
	  epollEventsIn[ i ].events |= EPOLLIN;
	  highestFd = i;
	}

      if( writeTran[ i ] != NULL )
	{
	  epollEventsIn[ i ].events |= EPOLLOUT;
	  highestFd = i;
	}

      if( epollEventsIn[ i ].events == 0 )
	epollTran[ i ] = NULL;
      else
	{
	  epollTran[ i ] = &epollDat;
	  if( epoll_ctl( epfd, EPOLL_CTL_ADD, i, &epollEventsIn[ i ] ) < 0 )
	    fprintf( stderr, "Epoll Control Add failed - %d %d %s\n", i, errno, strerror( errno ) );
	}
    }
#endif

  return 0;
}

int handleAccept( int fd )
{
#ifdef PRINT_LOOP_DEBUG
  //  fprintf( stderr, "Accepting new client on %d\n", fd );
#endif

  struct sockaddr_in cliAddr;
  socklen_t clilen = sizeof(cliAddr);

  int clientFd;
  httpRequest *presentCli;
  //  long arg;
  int status;
  int disable = 1;
  struct timeval now;
  
  
  for(;;)
    {
      if( presentClientCount >= cLimit )
	{
	  free( readTran[ fd ] );
	  readTran[ fd ] = NULL;
	  break;
	}

      clientFd = accept( fd, (struct sockaddr*)(&cliAddr), &clilen );
      
      if( clientFd == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) )
	{
	  break;
	}

      if( clientFd == -1 )
	{
	  fprintf( stderr, "Error accepting new connection on %d, errno=%d, %s\n", fd, errno, strerror( errno ) );
	  exit( 0 );
	  return 0;
	}

      if( clientFd == 0 )
	{
	  fprintf( stderr, "Pid %d %d accepts 0\n", getpid(), getppid() );
	  //return -1;
	}

      if( !checkClientAddress( &cliAddr ) )
	{
	  gettimeofday( &now, NULL );
	  genBufferLen = sprintf( genBuffer, "[%f] [error] [client %s] %s\n", timeVal( &now ), inet_ntoa( cliAddr.sin_addr ), "Unauthorized IP address" );
	  if( write( errorHelperWrite, genBuffer, genBufferLen ) != genBufferLen ) {
	    fprintf( stderr, "Error helper is busy\n" );
	  }

	  close( clientFd );
	  continue;
	}

      setsockopt( clientFd, IPPROTO_TCP, TCP_NODELAY, (char *) &disable, sizeof(int) );

      #if 0
      arg = fcntl( clientFd, F_GETFL, NULL );

      if( arg == -1 )
	{
	  fprintf( stderr, "Error getting the sock fcntl\n" );
	  return -1;
	}
      
      arg |= O_NONBLOCK;
      #endif
      status = fcntl( clientFd, F_SETFL, O_NONBLOCK );

      if( status == -1 )
	{
	  fprintf( stderr, "Error setting non blocking on socket\n" );
	  return -1;
	}
#if 0
      arg = fcntl( clientFd, F_GETFL, NULL );

      if( arg == -1 )
	{
	  fprintf( stderr, "Error getting the sock fcntl\n" );
	  return -1;
	}
      
      arg |= O_NDELAY;
      status = fcntl( clientFd, F_SETFL, arg );

      if( status == -1 )
	{
	  fprintf( stderr, "Error setting non blocking on socket\n" );
	  return -1;
	}
#endif

      presentClientCount++;

#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "O %d\n", clientFd );
#endif

      readTran[ clientFd ] = malloc( sizeof( httpRequest ) );
      readHandlers[ clientFd ] = &handleClientRead;
      presentCli = (httpRequest*)readTran[ clientFd ];
      initHTTPRequest( presentCli );
      presentCli->fd = clientFd;
      deleteHandlers[ clientFd ] = &checkClientExpiry;
      respTran[ clientFd ] = NULL;
      presentCli->clientIP.s_addr = (unsigned long)cliAddr.sin_addr.s_addr;
      connTran[ clientFd ] = NULL;
      //      total++;

      //if( total % 100 == 0 )
      //{
      // fprintf( stderr, "%d\n", total );
	//}
    }

  return 0;
}

int checkClientAddress( struct sockaddr_in *cliAddr )
{
  int i;
  unsigned long address;
  address = cliAddr->sin_addr.s_addr;

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "%lu\n", address );
#endif

  for( i = 0; i < subnetCount; i++ )
    {
      if( ( address & subnetMasks[ i ] ) == subnetMatches[ i ] )     
	return TRUE;
    }

  return FALSE;
}
      
int handleClientRead( int fd )
{
#ifdef PRINT_LOOP_DEBUG  
  gettimeofday( &now1, NULL );
  //  fprintf( stderr, "Client read " );
#endif

#ifdef PRINT_LOOP_DEBUG
  //  fprintf( stderr, "Received data %d\n", fd );
#endif
  
  int status;
  httpResponse *newRTranPtr, *presentRTranPtr;

  if( readTran[ fd ] == NULL )
    {
      fprintf( stderr, "Client request transaction not found\n" );
      return -1;
    }
  
  gettimeofday( &usageStamps[ fd ], NULL );
  
  httpRequest *presentQTranPtr = (httpRequest*)readTran[ fd ];
  presentQTranPtr->key.tv_sec = usageStamps[ fd ].tv_sec;
  presentQTranPtr->key.tv_usec = usageStamps[ fd ].tv_usec;

  if( presentQTranPtr == NULL )
    {
      fprintf( stderr, "Cannot find request transaction\n" );
      return -1;
    }
  
  if( presentQTranPtr->lDel == TRUE )
    {
      //exit( 0 );
      //      addToHead( delReqList, 0, (char*)presentQTranPtr );
      return 0;
    }

  status = parseRequest( fd, presentQTranPtr );

  if( status == -1 )
    {
      //exit( -1 );
      fprintf( stderr, "Bad request by client %d\n", fd );
      //exit( -1 );

      if( presentQTranPtr->status != CLOSED )
	fprintf( stderr, "Request cannot be parsed from fd %d\n", fd );

      status = delRequestTranLazy( presentQTranPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete client on %d\n", fd );
	  return -1;
	}

      if( presentQTranPtr->pstage == START )
	{
	  return 0;
	}

      return -1;
    }

  if( presentQTranPtr->status == CLOSED )
    {
      //fprintf( stderr, "Abrupt close by client %d\n", fd );
      //      exit( -1 );
      presentQTranPtr->method = GET;
      status = delRequestTranLazy( presentQTranPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete client on %d\n", fd );
	  return -1;
	}

      if( presentQTranPtr->pstage == START )
	{
	  return 0;
	}

      return 0;
    }

  if( presentQTranPtr->status == BAD_REQUEST )
    {
      fprintf( stderr, "Bad request\n" );
      // exit( -1 );
      status = delRequestTranLazy( presentQTranPtr );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot delete request\n" );
	  return -1;
	}

      if( presentQTranPtr->pstage == START )
	{
	  delRequestTranLazy( presentQTranPtr );
	  return 0;
	}

      return -1;
    }

  if( presentQTranPtr->status == BUFF_FULL && presentQTranPtr->method != POST )
    {
      fprintf( stderr, "Too large a request on %d\n", fd );
      delRequestTranLazy( presentQTranPtr );
      return 0;
    }

  if( presentQTranPtr->status == COME_BACK )
    {
      return 0;
    }

  if( presentQTranPtr->pstage == START )
    {
      presentQTranPtr->hash = getLocation( presentQTranPtr->URL, strlen( presentQTranPtr->URL ) );
      
      status = parseURL( &presentQTranPtr->info, presentQTranPtr->URL );

      if( status == -1 )
	{
	  // exit( -1 );
	  fprintf( stderr, "Cannot parse URL\n" );
	  return -1;
	}
    }

  switch( presentQTranPtr->pstage )
    {
    case START:
      switch( presentQTranPtr->method )
	{
	case GET: case HEAD: case WAPROX_GET:
#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "GET %s\n", presentQTranPtr->URL );
	    //    fprintf( stderr, "Joining a flow\n" );
#endif

#if WAPROX > 0
	  status = enQueueObject( presentQTranPtr->con_len, (char*)presentQTranPtr );
#else
	  status = joinFlow( presentQTranPtr );
#endif

	  if( status == -1 )
	    {
	      //exit( -1 );
	      fprintf( stderr, "Cannot join flow\n" );
	      return -1;
	    }

	  /*	  if( presentQTranPtr->method == GET )
	    {
	      readTran[ presentQTranPtr->fd ] = NULL;
	      readHandlers[ presentQTranPtr->fd ] = NULL;
	    }
	  */
	  break;
	case POST:
#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Received request for posting %s\n", presentQTranPtr->URL );
	  fprintf( stderr, "Creating a response\n" );
#endif
	  
	  newRTranPtr = (httpResponse*)malloc( sizeof( httpResponse ) );
	  initHTTPResponse( newRTranPtr, presentQTranPtr );
	  newRTranPtr->type = WEB;
	  newRTranPtr->cacheCheck = FALSE;

	  status = searchAndConnect( newRTranPtr );
	  
	  if( status == -1 )
	    {
	      fprintf( stderr, "Cannot search for conns and connect\n" );
	      return -1;
	    }

	  readTran[ presentQTranPtr->fd ] = NULL;
	  break;
	case WAPROX_PUT:
#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Received request for putting %s\n", presentQTranPtr->URL );
	  fprintf( stderr, "Creating a response\n" );
#endif
	  presentQTranPtr->hash = getLocation( presentQTranPtr->URL, strlen( presentQTranPtr->URL ) );
	  newRTranPtr = (httpResponse*)malloc( sizeof( httpResponse ) );
	  initHTTPResponse( newRTranPtr, presentQTranPtr );
	  
	  status = readyForPut( newRTranPtr );

	  if( status == -1 )
	    {
	      fprintf( stderr, "Bad put\n" );
	      return -1;
	    }

	  break;
	default:
	  fprintf( stderr, "Unknown HTTP method!\n" );
	  break;
	}
      
      if( presentQTranPtr->stage != REQUEST_LINE )
	{
	  presentQTranPtr->pstage = LATER;
	}

      break;
    case LATER:
      presentRTranPtr = (httpResponse*)respTran[ fd ];

      if( presentRTranPtr == NULL )
	{
	  fprintf( stderr, "Cannot find response transaction for this request\n" );
	  return -1;
	}

      if( presentQTranPtr->status == BUFF_FULL || 
	  presentQTranPtr->status == ENTITY_COMPLETE )
	{
	  status = sendRequestData( presentRTranPtr->fd, presentQTranPtr );
	  
	  if( status == 0 ) {
	    presentRTranPtr->used = 0;
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
	}

      break;
    }

  return 0;
}

int handleDataHelperRead( int fd )
{
#ifdef PRINT_LOOP_DEBUG  
  gettimeofday( &now1, NULL );
  //  fprintf( stderr, "Data Helper Read " );
#endif

  int status, i;
  dataRep presentDatReply;

  blockHeader *pHead[ WAYS ];
  blockTailer *pTail[ WAYS ];

  httpResponse *presentRTranPtr = NULL;
  mapEntry *presentMapEntry = NULL;

#ifdef PRINT_LOOP_DEBUG
  //  fprintf( stderr, "Reader map data\n" );
#endif

  status = read( fd, (char*)(&presentDatReply), sizeof( dataRep ) );

  if( status == -1 ) 
    {
      fprintf( stderr, "Error reading data reply\n" );
      return -1;
    }

  if( status == 0 )
    {
      fprintf( stderr, "Data helper killed\n" );
      exit( -1 );
    }

  returnElevator( diskSchedulers[ whichRem( presentDatReply.remOffset ) ] );
  int startRemOff = presentDatReply.remOffset;
  int numBlocksThis = presentDatReply.remLen / FILE_BLOCK_SIZE;
  eleRequest* eRequest = (eleRequest*)presentDatReply.entry;
  list* requests;
  data = presentDatReply.buffer;

  switch( presentDatReply.type )
    {
    case WRITE:
      requests = getMergedRequests( eRequest );
      startIter( requests );
      
      while( notEnd( requests ) == TRUE )
	{
	  presentMapEntry = (mapEntry*)(((diskRequest*)(getPresentData( requests ) ))->entry);
	  
	  for( i = 0; i < numBlocksThis; i++ )
	    {
	      startRemOff += numDisks;
	    }
	  
	  setLogFileLength( startRemOff );
	  startIter( presentMapEntry->presentList );
	      
	  while( notEnd( presentMapEntry->presentList ) != FALSE )
	    {
	      presentRTranPtr = (httpResponse*)getPresentData( presentMapEntry->presentList );
	      
	      presentRTranPtr->map--;
	      
	      if( presentRTranPtr->map == 0 ) {
		if( presentRTranPtr->lDel == TRUE )
		  presentRTranPtr->lDel = FALSE;
		
		removeWriteLock( presentRTranPtr->URL, 
				 presentRTranPtr->hash, 
				 presentRTranPtr->match );
		presentRTranPtr->batchAdd = FALSE;
		delResponseTranLazy( presentRTranPtr );
	      }
	      
	      deletePresent( presentMapEntry->presentList );

	      if( presentDatReply.isBatch == FALSE )
		returnSmallWriteBuffer( data );
	    }
	  
	  if( presentDatReply.isBatch == TRUE )
	    batchWriteLock -= 1;

	  ldestroy( presentMapEntry->presentList );
	  free( presentMapEntry );
	  totalMapEntryCount--;
	  move( requests );
	}

      deleteEleRequest( eRequest );
      scheduleNextRequest( diskSchedulers[ whichRem( presentDatReply.remOffset ) ] );
      return 0;
    case READ:
      for( i = 0; i < numBlocksThis; i++ )
	{
	  pHead[ 0 ] = (blockHeader*)(data + ( i * FILE_BLOCK_SIZE ));
	  pTail[ 0 ] = (blockTailer*)(data + ( ( i + 1 ) * FILE_BLOCK_SIZE ) - 
				      sizeof( blockTailer ));
	  insertBlock( (char*)pHead[ 0 ], pTail[ 0 ]->pres );
	}
      
      scheduleNextRequest( diskSchedulers[ whichRem( presentDatReply.remOffset ) ] );
      schedulePendingDiskRequests();
      requests = getMergedRequests( eRequest );
      
      startIter( requests );
      
      while( notEnd( requests ) == TRUE )
	{
	  presentMapEntry = (mapEntry*)(((diskRequest*)(getPresentData( requests ) ))->entry);
	  
	  startIter( presentMapEntry->presentList );
	  
	  while( notEnd( presentMapEntry->presentList ) != FALSE )
	    {
	      presentRTranPtr = (httpResponse*)getPresentData( presentMapEntry->presentList );
	      presentRTranPtr->remOffset = presentDatReply.remOffset;
	      presentRTranPtr->version = presentDatReply.version;
	      presentRTranPtr->map = FALSE;
	      processFurtherBlocks( presentRTranPtr );
	      deletePresent( presentMapEntry->presentList );
	    }
	  
	  ldestroy( presentMapEntry->presentList );
	  free( presentMapEntry );
	  move( requests );
	}
      
      deleteEleRequest( eRequest );
      return 0;
    }

   return 0;
}

int cacheHeaders( unsigned int hash, blockHeader **presentHeaders )
{
  int i, status;
  int setNum;
  indexHeader presIndexHeaders[ WAYS ];
  double ranks[ WAYS ];

  setNum = getSetFromHash( hash );

  for( i = 0; i < WAYS; i++ )
    {
      presIndexHeaders[ i ] = presentHeaders[ i ]->hash;
      ranks[ i ] = ((double)presentHeaders[ i ]->key.tv_sec) + ((double)(presentHeaders[ i ]->key.tv_usec) / 1000);
    }

#ifdef PRINT_LOOP_DEBUG
    fprintf( stderr, "Updating index for set %d\n", setNum );
#endif

  status = cacheSet( setNum, presIndexHeaders, ranks );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot update index cache\n" );
      return -1;
    }

  return 0;
}
      
int handleDNSHelperRead( int fd )
{
#ifdef PRINT_LOOP_DEBUG  
  gettimeofday( &now1, NULL );
  fprintf( stderr, "DNS Helper Read " );
#endif

  int status;
  DNSRep presentDNSReply;
  //  httpRequest *presentPTranPtr;
  httpResponse *presentDTranPtr;
  connection *presentConnPtr;
  DNSRequest *presentReqPtr;

  status = read( fd, (char*)(&presentDNSReply), sizeof( DNSRep ) );

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "DNS information obtained\n" );
#endif

  if( status == -1 ) 
    {
      fprintf( stderr, "Error reading data reply\n" );
      return -1;
    }
  
  if( status == 0 )
    {
      fprintf( stderr, "DNS helper killed\n" );
      exit( -1 );
    }

  presentReqPtr = (DNSRequest*)presentDNSReply.entry;
  
  //  hdel( asyncTab );

  if( hfind( dnsTranTab, presentReqPtr->server, strlen( presentReqPtr->server ) ) != TRUE )
    {
      fprintf( stderr, "Cannot find the dns tran tab entry %s\n", presentReqPtr->server );
      return -1;
    }

  hdel( dnsTranTab );

  if( presentDNSReply.status == 1 )
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Its a good DNS response\n" );
#endif

      startIter( presentReqPtr->connList );

      status = updateDNSCache( presentReqPtr->server, &presentDNSReply );
	  
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot update dns cache\n" );
	  return -1;
	}


      while( notEnd( presentReqPtr->connList ) == TRUE )
	{
	  presentConnPtr = (connection*)getPresentData( presentReqPtr->connList );

	  if( presentConnPtr->presentRes != NULL )
	    presentConnPtr->presentRes->connWait = FALSE;

	  memcpy( (char*)(&presentConnPtr->dnsInfo), (char*)(&presentDNSReply), sizeof( DNSRep ) );

#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Connecting to server for %s\n", presentConnPtr->server );
#endif
	  
	  status = connectServerLazy( presentConnPtr );

#ifdef PRINT_LOOP_DEBUG
	  //	  fprintf( stderr, "Server connection in progress\n" );
#endif

	  if( status == -1 )
	    {
	      fprintf( stderr, "Cannot connect to the server\n" );
	      return -1;
	    }

#ifdef PRINT_LOOP_DEBUG
	  //	  fprintf( stderr, "Updating DNS cache\n" );
#endif

	  move( presentReqPtr->connList );
	}
    }	    
  else
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Server is not found\n" );
#endif

      /*server not found - send error code*/

      startIter( presentReqPtr->connList );

      while( notEnd( presentReqPtr->connList ) == TRUE )
	{
	  presentConnPtr = (connection*)getPresentData( presentReqPtr->connList );
	  
	  presentDTranPtr = presentConnPtr->presentRes;
	  
	  if( presentDTranPtr != NULL )
	    presentDTranPtr->connWait = FALSE;

	  if( presentDTranPtr != NULL && presentDTranPtr->presReq != NULL )
	    {
	      signal( SIGPIPE, SIG_IGN );
	      status = write( presentDTranPtr->presReq->fd, getSNF(), SNF );  
	      
	      if( status == -1 )
		{
		  signal( SIGPIPE, SIG_IGN );
		}

	      delRequestTranLazy( presentDTranPtr->presReq );
	    }
	  
	  if( presentDTranPtr != NULL )
	    {
	      presentDTranPtr->type = -1;
	      delResponseTranLazy( presentDTranPtr );
	    }

	  delConnectionLazy( presentConnPtr );
	  move( presentReqPtr->connList );
	}
    }
  
#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Message from DNS helper %d processed\n", fd );
#endif 

  ldestroy( presentReqPtr->connList );
  free( presentReqPtr );
  return 0;
}

int handleDownloadRead( int fd )
{
#ifdef PRINT_LOOP_DEBUG  
  gettimeofday( &now1, NULL );
  fprintf( stderr, "Download Read" );
#endif

  int status;
  httpResponse *presentDTranPtr = (httpResponse*)readTran[ fd ];
  //  httpRequest *presentPTranPtr, *presentQTranPtr;
  //stamp now;
  //time_t timeTaken;
#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, " %s\n", presentDTranPtr->URL );
#endif

  gettimeofday( &usageStamps[ fd ], NULL );
  if( presentDTranPtr == NULL )
    {
      readTran[ fd ] = NULL;
      readHandlers[ fd ] = NULL;
      return 0;
    }

  if( presentDTranPtr->lDel == TRUE )
    {
      readTran[ fd ] = NULL;
      readHandlers[ fd ] = NULL;
      return 0;
    }

#ifdef PRINT_LOOP_DEBUG
  //  fprintf( stderr, "Response chunk obtained\n" );
#endif

  status = parseResponse( fd, NULL, -1, presentDTranPtr );

  connection *presentConn;
  if( status == -1 || presentDTranPtr->status == BAD_RESPONSE )
    {
      //      fprintf( stderr, "Bad web response\n" );

#ifdef PRINT_LOOP_DEBUG
      badCount++;
#endif
      
      presentConn = (connection*)connTran[ presentDTranPtr->fd ];

      if( presentConn != NULL )
	presentConn->inUse = FALSE;

      if( presentDTranPtr->presReq != NULL )
	presentDTranPtr->presReq->method = GET;
      delRequestTranLazy( presentDTranPtr->presReq );
      delResponseTranLazy( presentDTranPtr );
      delConnectionLazy( presentConn);
      
      /* presentDTranPtr->retries++;
      if( presentDTranPtr->retries <= MAX_RETRIES )
	{
	  if( presentDTranPtr->stage == RESPONSE_LINE )
	    {
	      status = searchAndConnect( presentDTranPtr );
	      
	      if( status == -1 )
		{
		  return -1;
		}
	    }
	  else
	    {
	      delRequestTranLazy( presentDTranPtr->presReq );
	      delResponseTranLazy( presentDTranPtr );
	    }
	}
      else
      {
	  delRequestTranLazy( presentDTranPtr->presReq );
	  delResponseTranLazy( presentDTranPtr );
	  	}*/

      return 0;
    }

  if( presentDTranPtr->status == COME_BACK )
    {
      return 0;
    }

  /*  if( presentDTranPtr->status == BUFF_FULL )
    {
      readTran[ presentDTranPtr->fd ] = NULL;
      readHandlers[ presentDTranPtr->fd ] = NULL;
      return 0;
      }*/

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Sending data to clients\n" );
#endif

  if( presentDTranPtr->sendHead == FALSE )
    {
      status = sendHeader( presentDTranPtr );
     
      if( status == 2 )
	{
	  readTran[ presentDTranPtr->fd ] = NULL;
	  readHandlers[ presentDTranPtr->fd ] = NULL;
	  writeTran[ presentDTranPtr->presReq->fd ] = (char*)presentDTranPtr;
	  writeHandlers[ presentDTranPtr->presReq->fd ] = &resumeDownload;
	  return 0;
	}

      presentDTranPtr->sendHead = TRUE;
      
      if( status == -1 )
	{
	  presentConn = (connection*)connTran[ presentDTranPtr->fd ];

	  delRequestTranLazy( presentDTranPtr->presReq );
	  delResponseTranLazy( presentDTranPtr );
	  presentConn->inUse = FALSE;
	  delConnectionLazy( presentConn );
	  
	  fprintf( stderr, "Cannot send header to client\n" );
	  return -1;
	}
    }

  status = sendData( presentDTranPtr );

  if( status == 2 )
    {
      readTran[ presentDTranPtr->fd ] = NULL;
      readHandlers[ presentDTranPtr->fd ] = NULL;
      writeTran[ presentDTranPtr->presReq->fd ] = (char*)presentDTranPtr;
      writeHandlers[ presentDTranPtr->presReq->fd ] = &resumeDownload;
      return 0;
    }

  if( status == -1 )
    {
      presentConn = (connection*)connTran[ presentDTranPtr->fd ];
      
      delRequestTranLazy( presentDTranPtr->presReq );
      delResponseTranLazy( presentDTranPtr );
      presentConn->inUse = FALSE;
      delConnectionLazy( presentConn );
      
      fprintf( stderr, "Cannot send data\n" );
      return -1;
    }
  
  if( presentDTranPtr->type == WEB )
    {
      if( presentDTranPtr->block == 1 )
	{
	  if( presentDTranPtr->used >= ACTUAL_BLOCK_SIZE - ADDL_HEAD_SIZE )
	    {
	      presentDTranPtr->used = 0;
	    }
	}
      else
	{
	  if( presentDTranPtr->used >= ACTUAL_BLOCK_SIZE )
	    {
	      presentDTranPtr-> used = 0;
	    }
      	}
      presentDTranPtr->block++;  
    }

  if( presentDTranPtr->cacheCheck == TRUE )
    {
      presentConn = (connection*)connTran[ presentDTranPtr->fd ];

#ifdef PRINT_DEBUG
      //      fprintf( stderr, "Cheking if the connection has been added to base\n" );
#endif

#ifdef PERSISTENT
      if( presentConn != NULL && presentConn->added == FALSE )
	{
	  if( presentDTranPtr->majVersion == 1 && presentDTranPtr->conn != NULL && 
	      strncmp( presentDTranPtr->conn, "close", 5 ) != 0 && 
	      strncmp( presentDTranPtr->conn, "Close", 5 ) != 0 )
	    {
#ifdef PRINT_LOOP_DEBUG
	      fprintf( stderr, "Eligible for persistent connection\n" );
#endif

	      presentConn->inUse = TRUE;
	      status = addConnectionToBase( presentConn );
	 
	      if( status == -1 )
		{
		  fprintf( stderr, "Cannot add connection to connection base\n" );
		  return -1;
		}
	    }
	}
#endif

#ifdef PRINT_LOOP_DEBUG
      //      fprintf( stderr, "Checking if the response is cacheable\n" );
#endif

      presentDTranPtr->cacheCheck = FALSE;

      checkCachability( presentDTranPtr );

      //presentDTranPtr->cacheable = FALSE;

      if( noCache == 1 )
	{
	  presentDTranPtr->cacheable = FALSE;
	}

      if( presentDTranPtr->cacheable == TRUE )
	{
	  presentDTranPtr->type = DOWN;

#ifdef PRINT_DEBUG
	  cacheable++;
#endif

#ifdef PRINT_LOOP_DEBUG
	  //  fprintf( stderr, "Response is cacheable\n" );
#endif
	  if( presentDTranPtr->con_len > LARGE_LIMIT )
	    {
	      presentDTranPtr->type = WEB;
	      presentDTranPtr->cacheable = FALSE;
	    }
	  else
	    {
	      addToBatch( (char*)&presentDTranPtr->clientIP, presentDTranPtr );
	      presentDTranPtr->batchAdd = TRUE;
	    }
	}
      else
	{

#ifdef PRINT_LOOP_DEBUG
	  //	  fprintf( stderr, "Response is not cacheable\n" );
#endif

	  presentDTranPtr->type = WEB;
	}

      if( presentDTranPtr->type == DOWN )
	{
	  presentDTranPtr->pstage = FIRST;
	}
    }

  status = checkForWrite( presentDTranPtr );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot check for write\n" );
      return -1;
    }

  if( presentDTranPtr->status == ENTITY_COMPLETE && presentDTranPtr->etype != CONN )
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Entity complete\n" );
#endif

      presentDTranPtr->logType = COMMONLOG;

#ifdef PRINT_LOOP_DEBUG
      total++;

      if( total % 50 == 0 )
	{
	  //#ifdef PRINT_LOOP_DEBUG
	  //	  fprintf( stderr, "g %d t %d b %d c %d m %d w %d nm %d h %d c %d s %d\n", totalGet, total, badCount, cacheable, tMem, writable, presentMapNum, highestFd, presentClientCount, presentConnCount );
      //#endif
	}
#endif
      
      presentConn = (connection*)connTran[ presentDTranPtr->fd ];

      if( persistentClient( presentDTranPtr->presReq ) == TRUE )
	{
	  reInitHTTPRequest( presentDTranPtr->presReq );
	}
      else
	{
	  delRequestTranLazy( presentDTranPtr->presReq );
	}

#ifdef USE_DIRTY_CACHE
      if( presentDTranPtr->cacheable == TRUE && presentDTranPtr->type != WEB )
	{
	  presentDTranPtr->dob = createDirtyObject( presentDTranPtr->URL, presentDTranPtr->allBlocks );
	}
#endif

      delResponseTranLazy( presentDTranPtr );

      if( presentConn != NULL )
	presentConn->inUse = FALSE;
      
      if( persistentConn( presentConn ) != TRUE )
	{
	  delConnectionLazy( presentConn );
	}
      else
	{
	  manageConnection( presentConn );
	}

      return 0;
    }

  if( presentDTranPtr->status == CLOSED || ( presentDTranPtr->status == ENTITY_COMPLETE && presentDTranPtr->etype == CONN ) )
    {
      presentConn = (connection*)connTran[ presentDTranPtr->fd ];

      if( presentDTranPtr->etype == CONN )
	{
#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Entity complete\n" );
#endif

#ifdef PRINT_LOOP_DEBUG
	  total++;

	  if( total % 50 == 0 )
	    {
	      //#ifdef PRINT_LOOP_DEBUG
	      //	      fprintf( stderr, "g %d t %d b %d c %d m %d w %d nm %d h %d c %d s %d\n", totalGet, total, badCount, cacheable, tMem, writable, presentMapNum, highestFd, presentClientCount, presentConnCount );
	      //#endif
	    }
#endif
	  
	  presentDTranPtr->logType = COMMONLOG;

	  presentConn = (connection*)connTran[ presentDTranPtr->fd ];
	  delRequestTranLazy( presentDTranPtr->presReq );
	  
#ifdef USE_DIRTY_CACHE
	  if( presentDTranPtr->cacheable == TRUE && presentDTranPtr->type != WEB )
	    {
	      presentDTranPtr->dob = createDirtyObject( presentDTranPtr->URL, presentDTranPtr->allBlocks );
	    }
#endif

	  delResponseTranLazy( presentDTranPtr );

	  if( presentConn != NULL )
	    presentConn->inUse = FALSE;

	  delConnectionLazy( presentConn );

      	  return 0;
	}
      else
	{
	  presentConn = (connection*)connTran[ presentDTranPtr->fd ];
	  
	  delRequestTranLazy( presentDTranPtr->presReq );
	  delResponseTranLazy( presentDTranPtr );

	  if( presentConn != NULL )
	    presentConn->inUse = FALSE;

	  delConnectionLazy( presentConn );

	  fprintf( stderr, "Premature termination of a download\n" );	  
	  return -1;
	}

      return 0;
    }

  return 0;
}

int checkForFreeBlocks( httpResponse *presentDTranPtr )
{
  int status;
  setHeader *presentHeaderPtr;

  presentHeaderPtr = getSetHeader( presentDTranPtr->hash );

  if( presentHeaderPtr == NULL )
    {
      fprintf( stderr, "Cannot get set header\n" );
      return 1;
    }

  if( isValid( presentHeaderPtr) == -1 )
    {
      presentDTranPtr->type = WEB;
      return 1;
    }

  status = getFreeLocation( presentHeaderPtr, presentDTranPtr->hash );

  if( status == -1 )
    {
      presentDTranPtr->type = WEB;
      return 1;
    }
  else
    {
      presentDTranPtr->match = status;
      presentDTranPtr->type = DOWN;
      presentDTranPtr->pstage = FIRST;

      status = addWriteLock( presentDTranPtr->URL, presentDTranPtr->hash, 
			     presentDTranPtr->match );

      if( status == 1 )
	{
	  fprintf( stderr, "Read locks on the object\n" );
	  presentDTranPtr->type = WEB;
	  presentDTranPtr->cacheable = FALSE;
	  return -1;
	}

      return 0;
    }
  
  return 1;
}

int getFreeLocation( setHeader *presentHeader, unsigned int hash )
{
  unsigned int bestTime = WAYS + 1;
#ifdef PRINT_LOOP_DEBUG
  unsigned int total = 0;
#endif
  unsigned int present;
  int bestMatch = -1;
  int i;

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "getting free location for hash %u set %u\n", hash, 
	   getSetFromHash( hash ) );
#endif

  for( i = 0; i < WAYS; i++ )
    {
      present = getClock( presentHeader, i );

#ifdef PRINT_LOOP_DEBUG
      total += present;
#endif

      if( lockOn( hash, i ) )
	{
	  continue;
	}

      if( present < bestTime )
	{
	  bestTime = present;
	  bestMatch = i;
	}
    }

#ifdef PRINT_LOOP_DEBUG
  setHeader* temp = getSetHeader( hash - 1 );
  if( total != 28 ) {
    for( i = 0; i < WAYS; i++ ) {
      fprintf( stderr, "%u %u\n", getClock( temp, i ), getClock( presentHeader, i) );
    }

    exit( -1 );
  }
#endif

  return bestMatch;
}

int checkForWrite( httpResponse *presentDTranPtr )
{
  int status;
  char* buffer;
  //  int unWritten = presentDTranPtr->downloaded - presentDTranPtr->written;
  
  if( presentDTranPtr->cacheable == TRUE && presentDTranPtr->type == DOWN )
    {
      if( presentDTranPtr->status == ENTITY_COMPLETE )
	{
#ifdef PRINT_HCFS_DEBUG
	  fprintf( stderr, "Preparing for a write\n" );
#endif
	  
	  readTran[ presentDTranPtr->fd ] = NULL;
	  readHandlers[ presentDTranPtr->fd ] = NULL;

	  while( getSize( presentDTranPtr->toWrite ) != 0 )
	    {
	      presentDTranPtr->map++;
	      
	      buffer = getSmallResponseBuffer();
	      status = prepareWrite( buffer, presentDTranPtr );
	      
	      if( status == -1 )
		{
		  returnSmallWriteBuffer( buffer );
		  fprintf( stderr, "Cannot schedule a write\n" );
		  return -1;
		}
	    }
	}
      else
	{
	  if( presentDTranPtr->block == 1 )
	    {
	      //if( unWritten >= ACTUAL_BLOCK_SIZE - ADDL_HEAD_SIZE ||  ( presentDTranPtr->status == ENTITY_COMPLETE && presentDTranPtr->written < presentDTranPtr->downloaded ) || ( presentDTranPtr->etype == CONN && presentDTranPtr->status == CLOSED && presentDTranPtr->written < presentDTranPtr->downloaded ) )
	      if( presentDTranPtr->used >= HTTP_ACT_BLOCK_SIZE - ADDL_HEAD_SIZE )
		{
#ifdef PRINT_HCFS_DEBUG
		  fprintf( stderr, "Preparing for a write\n" );
#endif

		  while( getSize( presentDTranPtr->toWrite ) != 0 )
		    {
		      presentDTranPtr->map++;
		      
		      buffer = getSmallResponseBuffer();
		      status = prepareWrite( buffer, presentDTranPtr );
		      
		      if( status == -1 )
			{
			  returnSmallWriteBuffer( buffer );
			  fprintf( stderr, "Cannot schedule a write\n" );
			  return -1;
			}
		    }
		}
	    }
	  else
	    {
	      //if( unWritten >= ACTUAL_BLOCK_SIZE || ( presentDTranPtr->status == ENTITY_COMPLETE && presentDTranPtr->written < presentDTranPtr->downloaded ) || ( presentDTranPtr->etype == CONN && presentDTranPtr->status == CLOSED && presentDTranPtr->written < presentDTranPtr->downloaded ) )
	      if( presentDTranPtr->used >= HTTP_ACT_BLOCK_SIZE )
		{
#ifdef PRINT_HCFS_DEBUG
		  fprintf( stderr, "Preparing for a write\n" );
#endif

		  while( getSize( presentDTranPtr->toWrite ) != 0 )
		    {
		      presentDTranPtr->map++;
		      
		      buffer = getSmallResponseBuffer();
		      status = prepareWrite( buffer, presentDTranPtr );
		      
		      if( status == -1 )
			{
			  returnSmallWriteBuffer( buffer );
			  fprintf( stderr, "Cannot schedule a write\n" );
			  return -1;
			}
		    }
		}
	    }
	}
    }

  return 0;
}


int handleConnect( int fd )
{
#ifdef PRINT_LOOP_DEBUG  
  gettimeofday( &now1, NULL );
  //  fprintf( stderr, "Connect Time " );
#endif

  int status;
  connection *presentConn = (connection*)writeTran[ fd ];
  httpResponse *presentRTranPtr = presentConn->presentRes;

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Server %s ready to take request\n", presentConn->server );
#endif

  if( presentRTranPtr == NULL )
    {
      delConnectionLazy( presentConn );
      return 0;
    }
  //  fprintf( stderr, "hello" );
  if( hfind( fTab, presentRTranPtr->URL, strlen( presentRTranPtr->URL ) ) == FALSE )
    { // fprintf( stderr, "hi3" );
      hadd( fTab, presentRTranPtr->URL, strlen( presentRTranPtr->URL ), (char*)presentRTranPtr );
      presentRTranPtr->flowEntry = TRUE;
    }

  if( presentRTranPtr == NULL )
    { // fprintf( stderr, "hi4" );
      if( presentConn->added == TRUE )
	{ // fprintf( stderr, "hi5" );
	  presentConn->inUse = FALSE;
	}
      else
	{ // fprintf( stderr, "hi6" );
	  delConnectionLazy( presentConn );
	}

      return 0;
    }

  presentRTranPtr->connWait = FALSE;
  //  fprintf( stderr, "hi7" );
  if( presentRTranPtr->lDel == TRUE )
    {//  fprintf( stderr, "hi8" );
      delResponseTranLazy( presentRTranPtr );
      delConnectionLazy( presentConn );
      return 0;
    }
  //  fprintf( stderr, "hi9" );
  presentConn->presentRes->fd = fd;
  status = sendRequest( presentConn->presentRes );
  //  fprintf( stderr, "hi10" );
  if( status == 2 )
    {
      writeTran[ presentConn->fd ] = (char*)presentConn;
      writeHandlers[ presentConn->fd ] = &handleConnect;
      presentRTranPtr->connWait = TRUE;
      return 0;
    }

  if( status == -1 )
    {
      presentRTranPtr = presentConn->presentRes;

      if( presentRTranPtr == NULL )
	{
	  if( presentConn->added == TRUE )
	    {
	      presentConn->inUse = FALSE;
	    }
	  else
	    {
	      delConnectionLazy( presentConn );
	    }

	  return 0;
	}
    
      presentRTranPtr->retries++;

      delConnectionLazy( presentConn );

      if( presentRTranPtr->retries >= MAX_RETRIES )
	{
	  delRequestTranLazy( presentRTranPtr->presReq );
	  delResponseTranLazy( presentRTranPtr );

	  fprintf( stderr, "Cannot start the next response transaction - tried twice\n" );
	  return 0;
	}

      status = searchAndConnect( presentRTranPtr );

      if( status == -1 )
	{
	  delRequestTranLazy( presentRTranPtr->presReq );
	  delResponseTranLazy( presentRTranPtr );

	  fprintf( stderr, "Cannot start the next response transaction\n" );
	  return -1;
	}

      return 0;
    }

  writeTran[ fd ] = NULL;
  writeHandlers[ fd ] = NULL;
  connTran[ fd ] = (char*)presentConn;
  readTran[ fd ] = (char*)(presentConn->presentRes);
  readHandlers[ fd ] = &handleDownloadRead;
  gettimeofday( &usageStamps[ fd ], NULL );
  deleteHandlers[ fd ] = &checkPersistentConnection;
  
  if( presentRTranPtr->method == POST )
    {
      readTran[ presentRTranPtr->presReq->fd ] = (char*)(presentRTranPtr->presReq);
      readHandlers[ presentRTranPtr->presReq->fd ] = &handleClientRead;
    }

  if( presentConn->presentRes == NULL )
    {
      fprintf( stderr, "Missing response object\n" );
      return -1;
    }

  return 0;
}

int handleTimerHelperRead( int fd )
{
#ifdef PRINT_LOOP_DEBUG  
  gettimeofday( &now1, NULL );
  //  fprintf( stderr, "Timer Helper Read\n" );
#endif

  int status, i;
  char n;
  stamp now;
  int (*handler)(int, stamp);
  useconds_t p = COME_AGAIN_TIME;

#ifdef PRINT_LOOP_DEBUG
  expireCount = 0;
#endif

  for( i = 0; i < numDisks; i++ )
    {
      scheduleNextRequest( diskSchedulers[ i ] );
    }

  gettimeofday( &now, NULL );

  status = read( fd, &n, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot receive alarm notification from timer helper\n" );
      return -1;
    }

  if( status == 0 )
    {
      fprintf( stderr, "Timer helper killed\n" );
      exit( -1 );
    }

    float gap = (float)(now.tv_sec - timeSinceCheck.tv_sec) + 
      (float)(now.tv_usec - timeSinceCheck.tv_usec)/1000000.00;

  if( gap > EXP_CHECK_TIME )
    {
      timeSinceCheck.tv_usec = now.tv_usec;
      timeSinceCheck.tv_sec = now.tv_sec;

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Checking all connections for expiry\n" );
#endif
  
      for( i = 0; i <= highestFd; i++ )
	{
	  handler = deleteHandlers[ i ];
	  
	  if( handler == NULL )
	    {
	      continue;
	    }
	  
#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Checking %d\n", i );
#endif
	  
	  status = (*handler)( i, now );
	  
	  if( status == -1 )
	    {
	      fprintf( stderr, "Cannot delete unused object\n" );
	      return -1;
	    }
	}

#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "E %d\n", expireCount );
#endif
    }

  if( write( timerHelperWrite, (char*)(&p), sizeof( useconds_t ) ) != sizeof( useconds_t ) ) {
    fprintf( stderr, "Timer helper is busy\n" );
  }

  return 0;

}

int resumeDownload( int fd )
{
#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Resuming download for client %d\n", fd );
#endif

  httpResponse *presentRTranPtr = (httpResponse*)respTran[ fd ];

  if( presentRTranPtr == NULL )
    {
      delRequestTranLazy( (httpRequest*)readTran[ fd ] );
      return 0;
    }

  writeTran[ fd ] = NULL;
  writeHandlers[ fd ] = NULL;
  readTran[ presentRTranPtr->fd ] = (char*)presentRTranPtr;
  readHandlers[ presentRTranPtr->fd ] = &handleDownloadRead;

  connection *presentConn = (connection*)connTran[ presentRTranPtr->fd ];

  int status;

  if( presentRTranPtr->sendHead == FALSE )
    {
      status = sendHeader( presentRTranPtr );

      if( status == 2 )
	{
	  writeTran[ fd ] = (char*)presentRTranPtr;
	  writeHandlers[ fd ] = &resumeDownload;
	  readTran[ presentRTranPtr->fd ] = NULL;
	  readHandlers[ presentRTranPtr->fd ] = NULL;
	  return 0;
	}

      if( status == -1 )
	{
	  delRequestTranLazy( presentRTranPtr->presReq );
	  delResponseTranLazy( presentRTranPtr );
	  delConnectionLazy( presentConn );
	  return 0;
	}

      presentRTranPtr->sendHead = TRUE;
    }

  status = sendData( presentRTranPtr );

  if( status == 2 )
    {
      writeTran[ fd ] = (char*)presentRTranPtr;
      writeHandlers[ fd ] = &resumeDownload;
      readTran[ presentRTranPtr->fd ] = NULL;
      readHandlers[ presentRTranPtr->fd ] = NULL;
      return 0;
    }

  if( status == -1 )
    {
      delRequestTranLazy( presentRTranPtr->presReq );
      delResponseTranLazy( presentRTranPtr );
      delConnectionLazy( presentConn );
      return 0;
    }

  if( presentRTranPtr->type == WEB )
    {
      if( presentRTranPtr->block == 1 )
	{
	  if( presentRTranPtr->used >= ACTUAL_BLOCK_SIZE - ADDL_HEAD_SIZE )
	    {
	      presentRTranPtr->used = 0;
	    }
	}
      else
	{
	  if( presentRTranPtr->used >= ACTUAL_BLOCK_SIZE )
	    {
	      presentRTranPtr-> used = 0;
	    }
      	}
      presentRTranPtr->block++;  
    }

  if( presentRTranPtr->cacheCheck == TRUE )
    {
      presentConn = (connection*)connTran[ presentRTranPtr->fd ];
      
#ifdef PRINT_DEBUG
      fprintf( stderr, "Cheking if the connection has been added to base\n" );
#endif

#ifdef PERSISTENT
      if( presentConn != NULL && presentConn->added == FALSE )
	{
	  if( presentRTranPtr->majVersion == 1 && presentRTranPtr->conn != NULL && 
	      strncmp( presentRTranPtr->conn, "close", 5 ) != 0 && 
	      strncmp( presentRTranPtr->conn, "Close", 5 ) != 0 )
	    {
#ifdef PRINT_LOOP_DEBUG
	      fprintf( stderr, "Eligible for persistent connection\n" );
#endif

	      presentConn->inUse = TRUE;
	      status = addConnectionToBase( presentConn );
	 
	      if( status == -1 )
		{
		  fprintf( stderr, "Cannot add connection to connection base\n" );
		  return -1;
		}
	    }
	}
#endif

#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Checking if the response is cacheable\n" );
#endif

      presentRTranPtr->cacheCheck = FALSE;

      checkCachability( presentRTranPtr );

      //presentRTranPtr->cacheable = FALSE;

      if( presentRTranPtr->cacheable == TRUE )
	{
	  presentRTranPtr->type = DOWN;

#ifdef PRINT_DEBUG
	  cacheable++;
#endif

#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Response is cacheable\n" );
#endif

	  if( presentRTranPtr->con_len > LARGE_LIMIT )
	    {
	      presentRTranPtr->type = WEB;
	      presentRTranPtr->cacheable = FALSE;
	    }
	  else
	    {
#ifdef PRINT_LOOP_DEBUG
	      fprintf( stderr, "Adding %s to batch\n", presentRTranPtr->URL );
#endif
	      addToBatch( (char*)&presentRTranPtr->clientIP, presentRTranPtr );
	      	      
	      presentRTranPtr->batchAdd = TRUE;
	      presentRTranPtr->type = DOWN;
	    }
	}
      else
	{

#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Response is not cacheable\n" );
#endif

	  presentRTranPtr->type = WEB;
	}

      if( presentRTranPtr->type == DOWN )
	{
	  presentRTranPtr->pstage = FIRST;
	}
    }

  status = checkForWrite( presentRTranPtr );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot check for write\n" );
      return -1;
    }
  
  if( presentRTranPtr->status == ENTITY_COMPLETE && presentRTranPtr->etype != CONN )
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Entity complete\n" );
#endif

      presentRTranPtr->logType = COMMONLOG;
      
#ifdef PRINT_LOOP_DEBUG
      total++;

      if( total % 50 == 0 )
	{
	  //#ifdef PRINT_LOOP_DEBUG
	  //	  fprintf( stderr, "g %d t %d b %d c %d m %d w %d nm %d h %d c %d s %d\n", totalGet, total, badCount, cacheable, tMem, writable, presentMapNum, highestFd, presentClientCount, presentConnCount );
      //#endif
	}
#endif
      
      presentConn = (connection*)connTran[ presentRTranPtr->fd ];

      if( persistentClient( presentRTranPtr->presReq ) == TRUE )
	{
	  reInitHTTPRequest( presentRTranPtr->presReq );
	}
      else
	{
	  delRequestTranLazy( presentRTranPtr->presReq );
	}

#ifdef USE_DIRTY_CACHE
      if( presentRTranPtr->cacheable == TRUE && presentRTranPtr->type != WEB )
	{
	  presentRTranPtr->dob = createDirtyObject( presentRTranPtr->URL, presentRTranPtr->allBlocks );
	}
#endif

      delResponseTranLazy( presentRTranPtr );

      if( presentConn != NULL )
	presentConn->inUse = FALSE;


      if( persistentConn( presentConn ) != TRUE )
	{
	  delConnectionLazy( presentConn );
	}
      else
	{
	  manageConnection( presentConn );
	}

      return 0;
    }

  if( presentRTranPtr->status == CLOSED || (presentRTranPtr->status == ENTITY_COMPLETE && presentRTranPtr->etype == CONN ))
    {
      presentConn = (connection*)connTran[ presentRTranPtr->fd ];

      if( presentRTranPtr->etype == CONN )
	{
#ifdef PRINT_LOOP_DEBUG
	  fprintf( stderr, "Entity complete\n" );
#endif

#ifdef PRINT_LOOP_DEBUG
	  total++;

	  if( total % 50 == 0 )
	    {
	      //#ifdef PRINT_LOOP_DEBUG
	      //	      fprintf( stderr, "g %d t %d b %d c %d m %d w %d nm %d h %d c %d s %d\n", totalGet, total, badCount, cacheable, tMem, writable, presentMapNum, highestFd, presentClientCount, presentConnCount );
	      //#endif
	    }
#endif
	  
	  presentRTranPtr->logType = COMMONLOG;
	  presentConn = (connection*)connTran[ presentRTranPtr->fd ];
	  delRequestTranLazy( presentRTranPtr->presReq );
	 
#ifdef USE_DIRTY_CACHE
	  if( presentRTranPtr->cacheable == TRUE && presentRTranPtr->type != WEB )
	    {
	      presentRTranPtr->dob = createDirtyObject( presentRTranPtr->URL, presentRTranPtr->allBlocks );
	    }
#endif	  

	  delResponseTranLazy( presentRTranPtr );

	  if( presentConn != NULL )
	    presentConn->inUse = FALSE;

	  delConnectionLazy( presentConn );

      	  return 0;
	}
      else
	{
	  presentConn = (connection*)connTran[ presentRTranPtr->fd ];
	  
	  delRequestTranLazy( presentRTranPtr->presReq );
	  delResponseTranLazy( presentRTranPtr );

	  if( presentConn != NULL )
	    presentConn->inUse = FALSE;
	  delConnectionLazy( presentConn );

	  fprintf( stderr, "Premature termination of a download\n" );	  
	  return -1;
	}

      return 0;
    }

  readTran[ presentRTranPtr->fd ] = (char*)presentRTranPtr;
  readHandlers[ presentRTranPtr->fd ] = &handleDownloadRead;
  writeTran[ fd ] = NULL;
  writeHandlers[ fd ] = NULL;

  return 0;
}

int resumeDiskRead( int fd )
{
#ifdef PRINT_LOOP_DEBUG
  fprintf( stderr, "Resuming disk read for %d\n", fd );
#endif

  httpResponse *presentRTranPtr = (httpResponse*)respTran[ fd ];

  if( presentRTranPtr == NULL )
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Lost response object\n" );
#endif

      delRequestTranLazy( (httpRequest*)readTran[ fd ] );
      return 0;
    }

  char this2 = FALSE;
  int status;

  if( presentRTranPtr->sendHead == FALSE )
    {
      status = sendHeader( presentRTranPtr );

      if( status == 2 )
	{
	  return 0;
	}

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot send data\n" );
	  removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, presentRTranPtr->match );
	  delRequestTranLazy( presentRTranPtr->presReq );
	  delResponseTranLazy( presentRTranPtr );
	  return 0;
	}

      presentRTranPtr->sendHead = TRUE;
    }

  status = sendData( presentRTranPtr );

  if( status == 2 )
    {
      return 0;
    }

  if( status == -1 )
    {
      fprintf( stderr, "Cannot send data\n" );
      delRequestTranLazy( presentRTranPtr->presReq );
      delResponseTranLazy( presentRTranPtr );
      return 0;
    }
  
#ifdef PRINT_LOOP_DEBUG
  //  fprintf( stderr, "fds are %d %d\n", fd, presentRTranPtr->presReq->fd );
#endif

  writeTran[ fd ] = NULL;
  writeHandlers[ fd ] = NULL;
  presentRTranPtr->used = 0;

  if( presentRTranPtr->status != ENTITY_COMPLETE )
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Disk response in transit\n" );
#endif

      presentRTranPtr->pstage = SENDING;
      this2 = TRUE;
    }
  else
    {
#ifdef PRINT_LOOP_DEBUG
      fprintf( stderr, "Disk response complete\n" );
#endif

      presentRTranPtr->logType = COMMONLOG;
      //removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, presentRTranPtr->match );

      if( persistentClient( presentRTranPtr->presReq ) == TRUE &&
	  presentRTranPtr->etype != CONN )
	{
	  reInitHTTPRequest( presentRTranPtr->presReq );
	}
      else
	{
	  delRequestTranLazy( presentRTranPtr->presReq );
	}

      delResponseTranLazy( presentRTranPtr );
      return 0;
    }

  if( this2 == TRUE )
    processFurtherBlocks( presentRTranPtr );

  return 0;
}

int resumePosting( int fd )
{
  int status;
  httpResponse *presentRTranPtr = (httpResponse*)writeTran[ fd ];
  httpRequest *presentQTranPtr = presentRTranPtr->presReq;

  status = sendRequestData( fd, presentQTranPtr );

  if( status == 0 )
    {
      writeTran[ fd ] = NULL;
      readTran[ fd ] = (char*)presentRTranPtr;
      readHandlers[ fd ] = &handleClientRead;
      writeHandlers[ fd ] = NULL;
    }

  if( status == -1 )
    {
      delConnectionLazy( (connection*)connTran[ fd ] );
      delResponseTranLazy( presentRTranPtr );
      delRequestTranLazy( presentQTranPtr );
      return -1;
    }

  if( status == 2 )
    {
      return 0;
    }

  return 0;
}

int processFurtherBlocks( httpResponse *presentRTranPtr )
{
  //  return 0;
  int status, i, len;

  blockHeader *pHead[ WAYS ];
  blockTailer *pTail[ WAYS ];
  char *aData[ WAYS ];
  char *data = NULL;

#if SET > 0
  blockHeader *temp;
#endif

  absLocs presentAbs;
  int presentRemOffset;
  int presentRemVersion;
  char hitWithoutSeek = FALSE, missWithoutSeek = FALSE;
  diskRequest* presentDiskRequestPtr;
  mapEntry* presentMapEntry;
  char this = TRUE, this2 = FALSE;
  int match = 0;

  for(;;)
    {
      if( presentRTranPtr->lDel == TRUE )
	{
	  return 0;
	}

      if( presentRTranPtr->status == ENTITY_COMPLETE || 
	  presentRTranPtr->block > LARGE_LIMIT / FILE_BLOCK_SIZE + 3 )
	{
	  fprintf( stderr, "How????\n" );
	  // exit( -1 );
	}

      presentRemOffset = presentRTranPtr->remOffset;
      presentRemVersion = presentRTranPtr->version;
      hitWithoutSeek = FALSE;
      missWithoutSeek = TRUE;

      switch( presentRTranPtr->cacheType )
	{
	case HOC:
#if SET
	  firstInSet = presentRTranPtr->remOffset * WAYS / WAYS;

	  for( i = 0; i < WAYS; i++ ) {
	    data = getBlock( presentRTranPtr->firstInSet + i );
	    
	    if( data == NULL ) {
	      missWithoutSeek = FALSE;
	      continue;
	    }
	    else {
	      temp = (blockHeader*)data;
	      
	      if( strncmp( temp->URL, presentRTranPtr->URL, 
			   URL_SIZE ) == 0 ) {
		hitWithoutSeek = TRUE;
		presentRTranPtr->remOffset = presentRTranPtr->hash / WAYS * WAYS + i;
		presentRTranPtr->totalLen = FILE_BLOCK_SIZE;
		presentRTranPtr->match = i;
		break;
	      }
	    }

	    if( missWithoutSeek == TRUE ) {
	      presentRTranPtr->type = DOWN;
	      presentRTranPtr->pstage = FIRST;
	      
	      if( searchAndConnect( presentRTranPtr ) == -1 ) {
		fprintf( stderr, "Cannot check and connect\n" );
		return -1;
	      }

	      return 0;
	    }
#endif

#if COMPLETE_LOG > 0 || SETMEM > 0
	  data = getBlock( presentRTranPtr->remOffset );
#endif

	  break;
#ifdef USE_DIRTY_CACHE
	case DOB:
	  data = getNextBlock( presentRTranPtr->dob );
	  
	  if( nowInHOC( presentRTranPtr->dob ) == TRUE )
	    {
	      pTail[ 0 ] = (blockTailer*)(data + FILE_BLOCK_SIZE - sizeof( blockTailer ) );
	      //presentRTranPtr->cacheType = HOC;
	      presentRTranPtr->remOffset = pTail[ 0 ]->pres;
	      presentRTranPtr->version = pTail[ 0 ]->version;
	      //deleteDirtyObject( presentRTranPtr->dob );
	      //presentRTranPtr->dob = NULL;
	    }

	  break;
#endif
	default:
	  fprintf( stderr, "Bad cache type\n" );
	  exit( -1 );
	}

      if( data == NULL )
	{
	  if( presentRTranPtr->map == TRUE )
	    return 0;

	  presentDiskRequestPtr = malloc( sizeof( diskRequest ) );

	  convertRelToAbsRead( presentRTranPtr->block, presentRTranPtr->hash, 
			   presentRTranPtr->match, presentRTranPtr->remOffset, 
			   presentRTranPtr->totalLen * FILE_BLOCK_SIZE, &presentAbs );

	  presentDiskRequestPtr->writeLen = presentAbs.mapsize / FILE_BLOCK_SIZE;
	  presentDiskRequestPtr->hash = presentRTranPtr->hash;
	  presentDiskRequestPtr->type = READ;
	  presentDiskRequestPtr->isBatch = FALSE;
	  presentDiskRequestPtr->block = presentRTranPtr->block + 1;
	  presentDiskRequestPtr->remOffset = presentRTranPtr->remOffset;
	  presentDiskRequestPtr->match = presentRTranPtr->match;

	  if( presentDiskRequestPtr->writeLen <= 0 )
	    {
	      fprintf( stderr, "How the hell??\n" );
	      fprintf( stderr, "%d %d\n", presentRTranPtr->totalLen, presentRTranPtr->block );
	      fprintf( stderr, "%d %d %d %d\n", presentRTranPtr->remOffset, presentRTranPtr->version, presentRTranPtr->cacheType, presentRTranPtr->hash );
	      fprintf( stderr, "%d\n", presentRTranPtr->con_len );
	      fprintf( stderr, "%s\n", presentRTranPtr->URL );
	      //	      exit( -1 );
	      free( presentDiskRequestPtr );
	      //delRequestTranLazy( presentRTranPtr->presReq );
	      //delResponseTranLazy( presentRTranPtr );
	      fprintf( stderr, "%lu %d %d %d\n", (unsigned long)timeSinceProcess.tv_sec, getRemVersion(), getRemOffsetDummy(), totalCacheCount );
	      fprintf( stderr, "Num locks: %d\n", lockCount() );
	      fprintf( stderr, "HOC locks: %d %d\n", getReadLockCount(), getWriteLockCount() );
	      printSetStats();
	      printRequestCount();
	      printBufferStats();
	      printHOCStats();
	      fprintf( stderr, "Num dirty: %d\n", getNumDirty() );
	      return 0;
	    }

	  presentDiskRequestPtr->type = READ;
	  presentDiskRequestPtr->isBatch = FALSE;
	  presentDiskRequestPtr->version = presentRTranPtr->version;
#ifdef USE_MEM_SYS
	  presentMapEntry = (mapEntry*)getChunk( mapEntryMemSys );
#else
	  presentMapEntry = malloc( sizeof( mapEntry ) );
#endif
	  totalMapEntryCount++;

	  presentMapEntry->presentList = create();
	  addToHead( presentMapEntry->presentList, 0, (char*)presentRTranPtr );
	  presentRTranPtr->map = TRUE;
	  presentDiskRequestPtr->entry = (char*)presentMapEntry;
	  
	  addToTail( helperWaitList, 0, (char*)presentDiskRequestPtr );
	  schedulePendingDiskRequests();
	  return 0;
	}

      for( i = 0; i < WAYS; i++ )
	{
	  pHead[ i ] = (blockHeader*)data;
	  pTail[ i ] = (blockTailer*)(data + FILE_BLOCK_SIZE - sizeof( blockTailer ));
	  aData[ i ] = (char*)( data + sizeof( blockHeader ) );
	}
	 
      presentRTranPtr->block++;

      this = TRUE;

      for(;;)
	{
	  if( presentRTranPtr->lDel == TRUE )
	    {
	      delResponseTranLazy( presentRTranPtr );
	      this = FALSE; break;
	    }
	  
	  switch( presentRTranPtr->type )
	    {
	    case CACHE: case FOLLOW:
	      switch( presentRTranPtr->pstage )
		{
		case MATCH:
#ifdef PRINT_LOOP_DEBUG
		  fprintf( stderr, "Getting the best match\n" );
#endif
		  
		  status = getBestMatch( presentRTranPtr, pHead, aData, &match );
		  
		  //		  gettimeofday( &now1, NULL );
		  
		  if( status == -1 )
		    {
		      fprintf( stderr, "Failed to perform match selection\n" );
		      if( presentRTranPtr->cacheType == HOC )
			doneReading( presentRemOffset);
		      this = FALSE; break;
		    }
		  
		  if( match == -1 )
		    {
#ifdef PRINT_LOOP_DEBUG
		      fprintf( stderr, "No match for URL in the set\n" );
#endif
		      
		      presentFalsePositive++;

		      if( presentRTranPtr->method == WAPROX_GET )
			{
			  sendNegReply( presentRTranPtr );
			  
			  if( persistentClient( presentRTranPtr->presReq ) == FALSE )
			    {
			      delRequestTranLazy( presentRTranPtr->presReq );
			    }
			  else
			    {
			      reInitHTTPRequest( presentRTranPtr->presReq );
			    }
			  
			  delResponseTranLazy( presentRTranPtr );
			  
			  this = FALSE;
			  if( presentRTranPtr->cacheType == HOC )
			    doneReading( presentRemOffset);
			  break;
			}
		      
		      if( presentRTranPtr->cacheType == HOC )
			doneReading( presentRemOffset);
	
		      this = FALSE;
		      readyForDownload( presentRTranPtr );
		      break;
		    }
		  
#ifdef PRINT_LOOP_DEBUG
		  fprintf( stderr, "A URL match found in element %d of the set\n", match );
#endif
		  
		  presentRTranPtr->pstage = COMPATIBILITY;
		  break;
		case COMPATIBILITY:
#ifdef PRINT_LOOP_DEBUG
		  //		  fprintf( stderr, "Disk response is %s\n", aData[ match ] );
#endif
		  
		  if( pTail[ match ]->readable < 0 || 
		      pTail[ match ]->readable > ACTUAL_BLOCK_SIZE )
		    {
		      fprintf( stderr, "Bad disk response\n" );
		      discardHeaders( presentRTranPtr->hash, presentRTranPtr->match );
		      if( presentRTranPtr->cacheType == HOC )
			doneReading( presentRemOffset);
		      readyForDownload( presentRTranPtr );
		      break;
		    }
		  
		  if( pTail[ match ]->status == NOT_END && 
		      checkVersion( pTail[ match ]->version, 
				    pTail[ match ]->next ) == -1 )
		    {		    
		      fprintf( stderr, "Object deleted\n" );
		      discardHeaders( presentRTranPtr->hash, presentRTranPtr->match );
		      if( presentRTranPtr->cacheType == HOC )
			doneReading( presentRemOffset);
		      readyForDownload( presentRTranPtr );
		      break;
		    }
		  
		  status = parseResponse( -1, aData[ match ], 
					  pTail[ match ]->readable, presentRTranPtr );

		  if( presentRTranPtr->cacheType == HOC )
		    doneReading( presentRemOffset);
		  
		  if( presentRTranPtr->status == BUFF_FULL )
		    {
		      fprintf( stderr, "Schedule mismatch\n" );
		      exit( -1 );
		    }
		  
		  if( status == -1 || presentRTranPtr->status == BAD_RESPONSE || 
		      presentRTranPtr->status == COME_BACK )
		    {
		      fprintf( stderr, "Response was %s\n", aData[ match ] );
		      discardHeaders( presentRTranPtr->hash, presentRTranPtr->match );
		      pHead[ match ]->key.tv_sec = -1;
		      fprintf( stderr, "Cannot parse disk response\n" );
		      readyForDownload( presentRTranPtr );
		      this = FALSE;
		      break;
		    }

		  status = checkExpiry( presentRTranPtr );
		  
		  if( status == -1 )
		    {
		      //#ifdef PRINT_LOOP_DEBUG
		      fprintf( stderr, "Response expired\n" );
		      //#endif
		      
		      discardHeaders( presentRTranPtr->hash, presentRTranPtr->match );
		      pHead[ match ]->key.tv_sec = -1;
		      readyForDownload( presentRTranPtr );
		      this = FALSE;
		      break;
		    }
		  
		  if( presentRTranPtr->presReq == NULL )
		    {
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  status = checkCompatibility( presentRTranPtr->presReq, 
					       presentRTranPtr );
		  
		  if( status == -1 )
		    {
		      //#ifdef PRINT_LOOP_DEBUG
		      fprintf( stderr, "Response not compatible\n" );
		      //#endif
		      
		      readyForDownload( presentRTranPtr );
		      this = FALSE;
		      break;
		    }
		  
		  addReadLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
		  
		  status = sendHeader( presentRTranPtr );
		  
		  if( status == 2 )
		    {
		      if( pTail[ match ]->status == END )
			{
			  presentRTranPtr->status = ENTITY_COMPLETE;
			  removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, 
					  match ); 
			}
		      
		      writeTran[ presentRTranPtr->presReq->fd ] = (char*)presentRTranPtr;
		      writeHandlers[ presentRTranPtr->presReq->fd ] = &resumeDownload;
		      
		      presentRTranPtr->version = pTail[ match ]->version;
		      presentRTranPtr->remOffset = pTail[ match ]->next;
		      presentRTranPtr->totalLen = pTail[ match ]->length;
		      presentRTranPtr->match = match;
		      presentRTranPtr->pstage = SENDING;
		      this = FALSE; break;
		    }
		  
#ifdef PRINT_LOOP_DEBUG
		  fprintf( stderr, "Header size %d\n", genBufferLen );
#endif
		  
		  presentRTranPtr->sendHead = TRUE;
		  
		  if( status == -1 )
		    {
		      fprintf( stderr, "Cannot send header\n" );
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, 
				      match );
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  if( presentRTranPtr->presReq == NULL )
		    {
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, 
				      match );
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  status = sendData( presentRTranPtr );
		  
		  if( status == 2 )
		    {
		      if( pTail[ match ]->status == END )
			{
			  presentRTranPtr->status = ENTITY_COMPLETE;
			  removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, 
					  match );
			}
		      else
			{
			  presentRTranPtr->version = pTail[ match ]->version;
			  presentRTranPtr->remOffset = pTail[ match ]->next;
			  presentRTranPtr->totalLen = pTail[ match ]->length;
			}
		      
		      writeTran[ presentRTranPtr->presReq->fd ] = (char*)presentRTranPtr;
		      writeHandlers[ presentRTranPtr->presReq->fd ] = &resumeDiskRead;
		      presentRTranPtr->match = match;
		      presentRTranPtr->pstage = SENDING;
		      
		      this = FALSE; break;
		    }
		  
		  if( status == -1 )
		    {
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
		      
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      
		      fprintf( stderr, "Cannot send data\n" );
		      
		      this = FALSE; break;
		    }
		  
		  presentRTranPtr->used = 0;
		  
		  if( pTail[ match ]->status == END )
		    {
#ifdef PRINT_LOOP_DEBUG
		      fprintf( stderr, "Disk response complete\n" );
#endif
		      
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
		      
		      if( persistentClient( presentRTranPtr->presReq ) == TRUE && 
			  presentRTranPtr->etype != CONN )
			{
			  reInitHTTPRequest( presentRTranPtr->presReq );
			}
		      else
			{
			  delRequestTranLazy( presentRTranPtr->presReq );
			}
		
		      presentRTranPtr->status = ENTITY_COMPLETE;
		      status = delResponseTranLazy( presentRTranPtr );
		      
		      if( status == -1 )
			{
			  fprintf( stderr, "Cannot delete response transaction\n" );
			}
	     
		      this = FALSE; break;
		    }
		  else
		    {
		      if( pTail[ match ]->status == NOT_END )
			{
#ifdef PRINT_LOOP_DEBUG
			  fprintf( stderr, "Disk response in transit at %d\n", pTail[ match ]->next );
#endif
			  presentRTranPtr->version = pTail[ match ]->version;
			  presentRTranPtr->remOffset = pTail[ match ]->next;
			  presentRTranPtr->totalLen = pTail[ match ]->length;
			  
			  this2 = TRUE;
			}
		      else
			{
			  fprintf( stderr, "Bad disk response 1\n" );
			  delRequestTranLazy( presentRTranPtr->presReq );
			  delResponseTranLazy( presentRTranPtr );
			  this = FALSE; break;
			}
		    }
		  
		  presentRTranPtr->match = match;
		  
		  presentRTranPtr->pstage = SENDING;
		  
		  this = FALSE; break;
		case SENDING:
		  match = presentRTranPtr->match;
		  
#ifdef PRINT_LOOP_DEBUG
		  fprintf( stderr, "Next block of %s at match %d\n", presentRTranPtr->URL, presentRTranPtr->match );
#endif
		  
		  len = strlen( presentRTranPtr->URL );
		  
		  if( len >= URL_SIZE )
		    len = URL_SIZE - 1;
		  
#ifdef PRINT_LOOP_DEBUG
		  fprintf( stderr, "Stored URL %s\n", pHead[ match ]->URL );
#endif
		  
		  if( strncmp( presentRTranPtr->URL, pHead[ match ]->URL, len ) != 0 )
		    {
		      /*fprintf( stderr, "A later block missing!\n" );
		      fprintf( stderr, "Plausible next block %d\n", pTail[ match ]->next );
		      fprintf( stderr, "%s\n%s\n", presentRTranPtr->URL, pHead[ match ]->URL );
		      fprintf( stderr, "%s\n", presentRTranPtr->cacheType==DOB?"DOB":"HOC" );*/
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  if( pTail[ match ]->readable < 0 || pTail[ match ]->readable > ACTUAL_BLOCK_SIZE )
		    {
		      fprintf( stderr, "A later block missing!\n" );
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  status = parseResponse( -1, aData[ match ], pTail[ match ]->readable, presentRTranPtr );
		  
		  if( presentRTranPtr->status == BUFF_FULL )
		    {
		      fprintf( stderr, "Schedule mismatch\n" );
		      exit( -1 );
		    }
		  
#ifdef PRINT_LOOP_DEBUG
		  fprintf( stderr, "Readable %d\n", pTail[ match ]->readable );
#endif
		  
		  if( presentRTranPtr->cacheType == HOC )
		    doneReading( presentRemOffset);

		  if( status == -1 )
		    {
		      fprintf( stderr, "Bad disk response 2\n" );
		      
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, presentRTranPtr->match );
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  if( presentRTranPtr->status == BAD_RESPONSE )
		    {
		      fprintf( stderr, "Bad disk response 3\n" );
		      
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, presentRTranPtr->match );
		      
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      
		      this = FALSE; break;
		    }

		  status = sendData( presentRTranPtr );
		  
		  if( status == 2 )
		    {
		      if( pTail[ match ]->status == END )
			{
			  presentRTranPtr->status = ENTITY_COMPLETE;
			  removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, match );
			}
		      else
			{
			  presentRTranPtr->version = pTail[ match ]->version;
			  presentRTranPtr->remOffset = pTail[ match ]->next;
			  presentRTranPtr->totalLen = pTail[ match ]->length;
			}
		      
		      writeTran[ presentRTranPtr->presReq->fd ] = (char*)presentRTranPtr;
		      writeHandlers[ presentRTranPtr->presReq->fd ] = &resumeDiskRead;
		      
		      presentRTranPtr->match = match;
		      presentRTranPtr->pstage = SENDING;
		      
		      this = FALSE; break;
		    }
		  
		  presentRTranPtr->used = 0;
		  
		  if( status == -1 )
		    {
		      fprintf( stderr, "Cannot send data\n" );
		      delRequestTranLazy( presentRTranPtr->presReq );
		      delResponseTranLazy( presentRTranPtr );
		      this = FALSE; break;
		    }
		  
		  if( pTail[ match ]->status == END )
		    {
#ifdef PRINT_LOOP_DEBUG
		      fprintf( stderr, "Disk response complete\n" );
#endif
		      
		      removeReadLock( presentRTranPtr->URL, presentRTranPtr->hash, presentRTranPtr->match );
		      
		      if( persistentClient( presentRTranPtr->presReq ) == TRUE && 
			  presentRTranPtr->etype != CONN )
			{
			  reInitHTTPRequest( presentRTranPtr->presReq );
			}
		      else
			{
			  delRequestTranLazy( presentRTranPtr->presReq );
			}

		      presentRTranPtr->status = ENTITY_COMPLETE;
		      status = delResponseTranLazy( presentRTranPtr );
		      
		      if( status == -1 )
			{
			  fprintf( stderr, "Cannot delete response transaction\n" );
			}
		      
		      this = FALSE; break;
		    }
		  else
		    {
		      if( pTail[ match ]->status == NOT_END )
			{
#ifdef PRINT_LOOP_DEBUG
			  fprintf( stderr, "Disk response in transit\n" );
#endif
			  presentRTranPtr->version = pTail[ match ]->version;
			  presentRTranPtr->remOffset = pTail[ match ]->next;
			  presentRTranPtr->totalLen = pTail[ match ]->length;
			  this2 = TRUE;
			}
		      else
			{
			  fprintf( stderr, "Bad disk response 4\n" );
			  delRequestTranLazy( presentRTranPtr->presReq );
			  delResponseTranLazy( presentRTranPtr );
			  this = FALSE; break;
			}
		    }
		  
		  this = FALSE; break;
		}
	      break;
	    }
	      
	  if( this == FALSE )
	    {
	      break;
	    }
	}

      if( this2 == FALSE )
	{
	  break;
	}

      this2 = FALSE;
    }

  return 0;
}

int schedulePendingDiskRequests()
{
  diskRequest* presentDiskRequestPtr;
  
  startIter( helperWaitList );
  
  char *buf = NULL;

  while( notEnd( helperWaitList ) != FALSE )
    {
      /* buf = getLargeBuffer();

      if( buf == NULL )
      return 0;*/
      
      presentRequestCount++;
      presentDiskRequestPtr = (diskRequest*)getPresentData( helperWaitList );
      presentDiskRequestPtr->buffer = buf;

      if( presentDiskRequestPtr->writeLen > READ_BUFFER_SIZE )
	{
	  presentDiskRequestPtr->writeLen = READ_BUFFER_SIZE;
	}

      presentDiskRequestPtr->writeLen *= FILE_BLOCK_SIZE;
      presentBytes += presentDiskRequestPtr->writeLen;
      deletePresent( helperWaitList );
      //write( dataHelperWrites[ whichDatHelper() ], (char*)presentDiskRequestPtr, sizeof( diskRequest ) );
      //free( presentDiskRequestPtr );
      addRequest( diskSchedulers[ whichRem( presentDiskRequestPtr->remOffset ) ], 
		  presentDiskRequestPtr );
    }

  return 0;
}

void printRequestCount()
{
  fprintf( stderr, "Disk request: %d\n", presentRequestCount - prevRequestCount );
  fprintf( stderr, "False positives: %d\n", presentFalsePositive - prevFalsePositive );
  fprintf( stderr, "Average disk read size: %f\n", (presentRequestCount == prevRequestCount?0:(((float)presentBytes) / ((float)(presentRequestCount - prevRequestCount))))/1024.0 );
  fprintf( stderr, "Average disk batch kb: %f\n", presentCacheCount == 0?0:(((float)presentBatchTotal) / ((float)presentCacheCount )));
  fprintf( stderr, "Average disk batch size: %f\n", presentCacheCount == 0?0:(((float)presentBatchCount) / ((float)presentCacheCount )));
  fprintf( stderr, "Average object size: %f\n", presentBatchCount == 0?0:(((float)presentBatchTotal) / ((float)presentBatchCount )));
  fprintf( stderr, "Average IP batch size: %f\n", presentIPCount == 0?0:(((float)presentIPBatchCount) / ((float)presentIPCount )));
  fprintf( stderr, "Num IPs synced: %d\n", presentIPCount );
  fprintf( stderr, "Num objects synced: %d\n", presentBatchCount );
  prevRequestCount = presentRequestCount;
  prevFalsePositive = presentFalsePositive;
  presentBytes = 0;
  presentBatchTotal = 0;
  presentBatchCount = 0;
  presentCacheCount = 0;
  presentIPBatchCount = 0;
  presentIPCount = 0;
}
