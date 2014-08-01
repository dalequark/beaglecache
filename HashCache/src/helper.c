#include "helper.h"

volatile int toucher;
extern int numDisks;

int dataHelper( void* dat )
{
  helperDat *presentDat = (helperDat*)dat;
  int inFd = presentDat->sockFd;
  int outFd = presentDat->sockFd;
  char *wStatus = presentDat->helperStatus;

#ifdef PRINT_HELPER_DEBUG
  fprintf( stderr, "Data Helper %d %d starting....\n", inFd, outFd );
#endif

  int status;
  diskRequest presentDiskReq;
  absLocs presentAbs;

#ifdef PRINT_HELPER_DEBUG
  fprintf( stderr, "Disk helper %d %d starting pid %d\n", inFd, outFd, getppid() );
#endif

#ifdef PRINT_HELPER_DEBUG  
  struct timeval now1, now2;
  float timeTaken;
#endif

  //  int i;
  //for( i = 0; i < 10; i++)
  for(;;)
    {
      *wStatus = 0;
      //      logRemInfo();
      status = read( inFd, (char*)(&presentDiskReq), sizeof( diskRequest ) );

#ifdef PRINT_HELPER_DEBUG
      gettimeofday( &now1, NULL );
      //fprintf( stderr, "Data Helper %d waiting for requests\n", inFd );
#endif

      if( status == -1 && errno == EINTR )
	{
	  fprintf( stderr, "Data Helper %d : Interrupted read\n", inFd );
	  continue;
	}

      if( status == -1 )
	{
	  fprintf( stderr, "Data Helper %d : Cannot read disk request\n", inFd );
	  close( inFd );
	  exit( -1 );
	}

      if( status == 0 )
	{
	  fprintf( stderr, "Data Helper %d : Nothing to read\n", inFd );
	  continue;
	}

      *wStatus = 1;

#ifdef PRINT_HELPER_DEBUG
      fprintf( stderr, "Data Helper %d: Received request to map block %d"\
	       " of hash %d remoff %d\n", inFd, 
	       presentDiskReq.block, presentDiskReq.hash, presentDiskReq.remOffset );
      fprintf( stderr, ".....at location %d\n", (int)presentDiskReq.buffer );
#endif

      /*if( checkLimits( presentDiskReq.buffer ) == -1 )
	{
	  fprintf( stderr, "Buffer out of bounds\n" );
	  exit( -1 );
	  }*/

      status = convertRelToAbsRead( presentDiskReq.block, presentDiskReq.hash, 
				    presentDiskReq.match, presentDiskReq.remOffset, 
				    presentDiskReq.writeLen, &presentAbs );

      
      if( status == -1 )
	{
	  fprintf( stderr, "Data Helper %d: Error converting relative to"\
		   " absolute address\n", inFd );
	  close( inFd );
	  exit( -1 );
	}

#ifdef PRINT_HELPER_DEBUG
      fprintf( stderr, "Data Helper %d: Mapsize %d, location %lld, filedes"\
	       "%d\n", inFd, presentAbs.mapsize, presentAbs.offset, presentAbs.objFd );
#endif

      switch( presentDiskReq.type )
	{
	case READ:
	  status = pread( presentAbs.objFd, presentDiskReq.buffer, 
			  presentAbs.mapsize, presentAbs.offset );

#ifdef PRINT_HELPER_DEBUG
	  fprintf( stderr, "Data Helper %d: Using pread fd: %d"\
		   " location %llu size %d buffer %d\n", inFd, 
		   presentAbs.objFd, presentAbs.offset, presentAbs.mapsize, 
		   (int)presentDiskReq.buffer );
#endif

	  if( status < 0 )
	    {
	      fprintf( stderr, "Data Helper %d: pread failed - %s\n", 
		       inFd, strerror( errno ) );
	      close( inFd );
	      exit( -1 );
	    }

	  if( status == 0 )
	    {
	      fprintf( stderr, "Data Helper %d: pread could not"\
		       " read %d %d %u %llu\n", inFd, presentAbs.objFd, 
		       presentAbs.mapsize, (unsigned)presentDiskReq.buffer, 
		       presentAbs.offset );
	      exit( -1 );
	    }

	  break;
	case WRITE:
	  status = convertRelToAbsWrite( presentDiskReq.hash, presentDiskReq.match, 
					 presentDiskReq.block, presentDiskReq.remOffset, 
					 presentDiskReq.writeLen, &presentAbs );

	  if( status == -1 )
	    {
	      fprintf( stderr, "Data Helper %d: Cannot convert rel"\
		       " to abs write\n", inFd );
	      close( inFd );
	      exit( -1 );
	    }

#ifdef PRINT_HELPER_DEBUG
	  fprintf( stderr, "Data Helper %d: Using pwrite fd: %d, location"\
		   " %llu, size %d, buffer %d\n", inFd, presentAbs.objFd, 
		   presentAbs.offset, presentAbs.mapsize, 
		   (int)presentDiskReq.buffer );  
#endif

	  status = pwrite( presentAbs.objFd, presentDiskReq.buffer, 
			   presentAbs.mapsize, presentAbs.offset );

	  if( status < 0 )
	    {
	      fprintf( stderr, "Data Helper %d: pwrite failed - %s\n", inFd, strerror( errno ) );
	      close( inFd ); 
	      exit( -1 );
	    }

	  if( status == 0 )
	    {
	      fprintf( stderr, "Data Helper %d: pwrite could not write any\n", inFd );
	    }

	 break;
	}

      status = sendDataReply( outFd, &presentDiskReq.key, presentDiskReq.type, 
			      presentDiskReq.buffer, presentDiskReq.entry, 
			      presentDiskReq.isBatch, presentAbs.remOffset, 
			      presentDiskReq.version, presentAbs.mapsize );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Data Helper %d: Error in sending reply to proxy\n", inFd );
	  close( inFd );
	  exit( -1 );
	}

#ifdef PRINT_HELPER_DEBUG
      gettimeofday( &now2, NULL );
      timeTaken = (now2.tv_sec - now1.tv_sec)*1000 + (now2.tv_usec - now1.tv_usec)/1000;
      fprintf( stderr, "Data Helper %d: Loop time %f\n", inFd, timeTaken );
#endif
    } 

  exit( 0 );
}

#if USE_DNS_CLONE > 0
int dnsHelper( void *dat )
#else
int dnsHelper( int inFd, int outFd )
#endif
{
#if USE_DNS_CLONE > 0
  helperDat *presentHelper = (helperDat*)dat;
  int inFd = presentHelper->sockFd;
  int outFd = presentHelper->sockFd;
  char *wStatus = presentHelper->helperStatus; 
#endif

  int status, trials;
  DNSRequest presentDNSReq;
  struct hostent *presentEnt = NULL;

#ifdef PRINT_HELPER_DEBUG
  fprintf( stderr, "DNS helper %d %d starting with pid %d %d\n", inFd, outFd, getpid(), getppid() );
#endif

  //int i;
  //for( i = 0; i < 10; i++)
  for(;;)
    {
      //      logRemInfo();
      presentEnt = NULL;

#ifdef PRINT_HELPER_DEBUG
      fprintf( stderr, "DNS Helper %d waiting for requests\n", inFd );
#endif

#if USE_DNS_CLONE > 0
      *wStatus = 0; 
#endif

      status = read( inFd, (char*)(&presentDNSReq), sizeof( DNSRequest ) );

      if( status == -1 )
	{
	  fprintf( stderr, "DNS Helper %d: Error reading DNS request\n", inFd );
	  close( inFd );
	  exit( -1 );
	}
#ifdef PRINT_HELPER_DEBUG
      fprintf( stderr, "DNS Helper %d: Have to resolve %s\n", inFd, presentDNSReq.server );
#endif

#if USE_DNS_CLONE > 0
      *wStatus = 1;
#endif

      trials = 2;

      do
	{
	  presentEnt = gethostbyname( presentDNSReq.server );
	  trials++;
	}
      while( presentEnt == NULL && trials < 1 );
      
      if( presentEnt != NULL && presentEnt->h_addr_list[ 0 ] != NULL )
	{
#ifdef PRINT_HELPER_DEBUG
	  fprintf( stderr, "DNS Helper %d: %s resolved as %s\n", inFd, presentDNSReq.server, inet_ntoa( *(struct in_addr*)presentEnt->h_addr_list[ 0 ] ) );
#endif
	  
	  status = sendDNSReply( outFd, &presentDNSReq.key, 1, presentEnt->h_addr_list[ 0 ], presentDNSReq.entry );
	}
      else
	{
#ifdef PRINT_HELPER_DEBUG
	  fprintf( stderr, "DNS Helper %d: Cannot resolve %s\n", inFd, presentDNSReq.server );
#endif
	  
	  status = sendDNSReply( outFd, &presentDNSReq.key, 0, NULL, presentDNSReq.entry );
	}
      
      if( status == -1 )
	{
	  fprintf( stderr, "Helper %d: Error sending DNS reply\n", inFd );
	  close( inFd );
	  exit( -1 );
	}
    }
     
  exit( 0 );
}

int timerHelper( void *dat )
{
  helperDat *presentDat = (helperDat*)dat;
  int inFd = presentDat->sockFd;
  int outFd = presentDat->sockFd;
  int status;
  useconds_t p = 120000000;
  char n;

  for(;;)
    {
      status = readn( inFd, (char*)(&p), sizeof( useconds_t ) );

#ifdef PRINT_HELPER_DEBUG
      // fprintf( stderr, "Recevied an alarm for %ld usec\n", (long int)p );
#endif
 
      if( status == -1 || p <= 0 )
	{
	  fprintf( stderr, "Cannot determine notification time\n" );
	  close( inFd );
	  exit( -1 );
	}

      usleep( p );

      status = write( outFd, (char*)(&n), 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot notify main process about the alarm\n" );
	  close( inFd );
	  exit( -1 );
	}
    }

  exit( 0 );
}      
  
int sendDataReply( int outFd, keyvalue *key, char type, char *buffer, char *entry, char isBatch, int remOffset, int version, int remLen )
{
  dataRep presentReply;

#ifdef PRINT_HELPER_DEBUG
  fprintf( stderr, "Sending reply for map request generated at %d %d\n", (int)key->tv_sec, (int)key->tv_usec );
#endif
 
  memcpy( (char*)(&presentReply.key), (char*)key, sizeof( keyvalue ) );
  presentReply.type = type;
  presentReply.buffer = buffer;
  presentReply.entry = entry;
  presentReply.isBatch = isBatch;
  presentReply.remOffset = remOffset;
  presentReply.version = version;
  presentReply.remLen = remLen;
  if( write( outFd, (char*)(&presentReply), sizeof( dataRep ) ) <= 0 )
    exit( -1 );

  return 0;
}

int sendDNSReply( int outFd, keyvalue *key, int status, char *host, char *entry ) 
{
  DNSRep presentReply;
  
  memcpy( (char*)(&presentReply.key), (char*)key, sizeof( keyvalue ) );
  presentReply.status = status;
  presentReply.entry = entry;

  if( host != NULL )
    {
      memcpy( presentReply.presentHost, host, sizeof( struct in_addr ) );
    }

  if( write( outFd, (char*)(&presentReply), sizeof( DNSRep ) ) <= 0 )
    exit( -1 );

  return( 0 );
}

/*
int logHelper( int inFd )
{
  int status;

  remove( "log/hashcache.log" );

  int logFd = open( "log/hashcache.log", O_CREAT | O_RDWR, S_IRWXU );

  for(;;)
    {
      status = readn( inFd, (char*)(&p), sizeof( useconds_t ) );

#ifdef PRINT_HELPER_DEBUG
      fprintf( stderr, "Recevied an alarm for %ld usec\n", (long int)p );
#endif
 
      if( status == -1 || p <= 0 )
	{
	  fprintf( stderr, "Cannot determine notification time\n" );
	  exit( -1 );
	}

      usleep( p );

      status = write( outFd, (char*)(&n), 1 );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot notify main process about the alarm\n" );
	  exit( -1 );
	}
    }
}      


*/


int logHelper( void* dat )
{
  helperDat *presentDat = (helperDat*)dat;
  int inFd = presentDat->sockFd;
  //int outFd = (int)dat;
  char buffer[ 10000 ];
  int status;
  struct timeval now;

  gettimeofday( &now, NULL );
  char logFileName[ 100 ];

  sprintf( logFileName, "%s/common-%lu-%lu", logFilePath, (unsigned long int)now.tv_sec, (unsigned long int)now.tv_usec );
  fprintf( stderr, "Common Log: %s\n", logFileName );

  int logFd = open( logFileName, O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );

  for(;;)
    {
      //      logRemInfo();

      status = read( inFd, buffer, 10000 );

      if( status <= 0 )
	{
	  fprintf( stderr, "Log Helper: Error in read\n" );
	  exit( -1 );
	}

      status = write( logFd, buffer, status );

      if( status <= 0 )
	{
	  fprintf( stderr, "Log Helper: Error in write\n" );
	  exit( -1 );
	}
    }
}

int errorHelper( void* dat )
{
  helperDat *presentDat = (helperDat*)dat;
  int inFd = presentDat->sockFd;
  //int outFd = (int)dat;

  char buffer[ 10000 ];
  int status;
  struct timeval now;

  gettimeofday( &now, NULL );
  char logFileName[ 100 ];

  sprintf( logFileName, "%s/error-%lu-%lu", logFilePath, (unsigned long int)now.tv_sec, (unsigned long int)now.tv_usec );
  fprintf( stderr, "Error Log: %s\n", logFileName );

  int logFd = open( logFileName, O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );

  for(;;)
    {
      //      logRemInfo();

      status = read( inFd, buffer, 10000 );

      if( status <= 0 )
	{
	  fprintf( stderr, "Log Helper: Error in read\n" );
	  exit( -1 );
	}

      status = write( logFd, buffer, status );

      if( status <= 0 )
	{
	  fprintf( stderr, "Log Helper: Error in write\n" );
	  exit( -1 );
	}
    }
}

int indexHelper( void* dat )
{
  helperDat *presentDat = (helperDat*)dat;
  int inFd = presentDat->sockFd;
  char buffer[ 10000 ];
  int status;
  struct timeval now;

  gettimeofday( &now, NULL );
  char indexFileName[ 100 ];

  sprintf( indexFileName, "%s/index", indexFilePath );
  fprintf( stderr, "Index file: %s\n", indexFileName );    
  int indexFd = open( indexFileName, 
		      O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );

  if( indexFd < 0 ) {
    fprintf( stderr, "%s\n", strerror( errno ) );
    exit( -1 );
  }

  sprintf( indexFileName, "%s/offsets", indexFilePath );
  int offFd = open( indexFileName, 
		    O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );

  if( offFd < 0 ) {
    fprintf( stderr, "%s\n", strerror( errno ) );
    exit( -1 );
  }

  char* mem = malloc(8192);
  char* num = (char*)((unsigned)(mem + 4095) & ~(size_t)4095);
  int count = 0;
  int i;

  close( indexFd );
  close( offFd );

  sprintf( indexFileName, "%s/index", indexFilePath );
  indexFd = open( indexFileName, 
		  O_DIRECT | O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );

  if( indexFd < 0 ) {
    fprintf( stderr, "%s\n", strerror( errno ) );
    exit( -1 );
  }

  sprintf( indexFileName, "%s/offsets", indexFilePath );
  offFd = open( indexFileName, 
		O_DIRECT | O_CREAT | O_RDWR | O_LARGEFILE, S_IRWXU );

  if( offFd < 0 ) {
    fprintf( stderr, "%s\n", strerror( errno ) );
    exit( -1 );
  }

  for(;;)
    {
      status = read( inFd, buffer, 10000 );

      if( status <= 0 )
	{
	  fprintf( stderr, "Index Helper: Error in read\n" );
	  exit( -1 );
	}

      fprintf( stderr, "Writing index %d size\n", getIndexLen() );

      status = pwrite( indexFd, getIndexAddr(), getIndexLen(), getIndexOff() );

      if( status <= 0 )
	{
	  fprintf( stderr, "Index Helper: Error in index write - %s\n", 
		   strerror( errno ) );
	  exit( -1 );
	}

      count = 0;
      *(int*)(num + count) = getRemVersion();
      fprintf( stderr, "Writing Rem Version %d\n", *(int*)(num + count) );
      count += sizeof( int );
      
      for( i = 0; i < numDisks; i++ ) {
	*(int*)(num + count) = getRemOffset( i );
	fprintf( stderr, "Writing Rem Version %d: %d\n", i, *(int*)(num + count) );
	count += sizeof( int ); 
      }

      status = pwrite( offFd, num, 512, 0 );

      if( status <= 0 )
	{
	  fprintf( stderr, "Index Helper: Error in offsets write - %s\n", 
		   strerror( errno ) );
	  exit( -1 );
	}

    }
}
