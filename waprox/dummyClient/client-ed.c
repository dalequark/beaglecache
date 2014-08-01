#include <stdio.h>
#include <sys/select.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>
#include <sched.h>

#define BUFF_LEN 10000
#define PRINT_DEBUG 10
#define TIME_GAP 10000000

typedef struct traceData {
  char URL[ 1000 ];
} traceData;

static char *fileName;
static char *STATIC_SERV_ADDR;
int STATIC_SERV_PORT;
int status;
char buffer[ BUFF_LEN + 1 ];
fd_set readFDSet, writeFDSet;
char* readTran[ FD_SETSIZE ];
char* writeTran[ FD_SETSIZE ];
char* delTran[ FD_SETSIZE ];
char *hi = "hi";
int (*readHandlers[ FD_SETSIZE ])( int );
int (*writeHandlers[ FD_SETSIZE ])( int );
int numReady;
int (*handler)(int);
char presentRequest[ 1000 ];
int highestFd;
int numCli;
int count;
struct timeval timeOut;
struct timeval usageStamps[ FD_SETSIZE ];
traceData *presentTracePtr;
FILE *traces;


int connectServer();
int handleDownloadRead( int fd );
int handleConnect( int fd );
int makeFDSets();
int timerHelperRead();

int main( int argc, char** argv )
{
  if( argc != 6 )
    {
      fprintf( stderr, "Wrong number of arguments\n" );
      return 0;
    }

  char data[ 100 ];
  numCli = atoi( argv[ 1 ] );
  fileName = argv[ 2 ];
  STATIC_SERV_ADDR = argv[ 3 ];
  STATIC_SERV_PORT = atoi( argv[ 4 ] );
  presentTracePtr = (traceData*)malloc( sizeof( traceData ) );
  traces = fopen( fileName, "r" );

  //int get = GET;  int status;
  int i;
  int status;
  count = numCli;

  for( i = 0; i < FD_SETSIZE; i++ )
    {
      readTran[ i ] = NULL;
      writeTran[ i ] = NULL;
      delTran[ i ] = NULL;
    }

  for( i = 0; i < numCli; i++ )
    {
#ifdef PRINT_DEBUG
      printf( "Client %d : Initiating system\n", i );
#endif

      connectServer();
    }
  
  timeOut.tv_sec = 10;
  timeOut.tv_usec = 0;

  for(;;)
    {
      makeFDSets();

      status = select( highestFd, &readFDSet, &writeFDSet, NULL, &timeOut );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot select %d %s\n", errno, strerror( errno ) );
	  return -1;
	}

      if( status == 0 )
	{
	  timerHelperRead();
	  makeFDSets();
	  timeOut.tv_sec = 10;
	  timeOut.tv_usec = 0;
	  continue;
	}

      for( i = 0; i < highestFd + 1; i++ )
	{
	  if( FD_ISSET( i, &readFDSet ) )
	    {
#ifdef PRINT_DEBUG
	      fprintf( stderr, "fd %d ready for read\n", i );
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
		  return -1;
		}
	    }
	      
	  if( FD_ISSET( i, &writeFDSet ) )
	    {
#ifdef PRINT_DEBUG
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
	    }
	}
    }
}

int connectServer()
{
  int sockfd;
  struct sockaddr_in servAddr;
  int disable = 1;

  sockfd = socket( AF_INET, SOCK_STREAM, 0 );
  bzero( &servAddr, sizeof( servAddr ) );
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = inet_addr( STATIC_SERV_ADDR );
  servAddr.sin_port = htons( STATIC_SERV_PORT );

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

  status = fcntl( sockfd, F_SETFL, O_NONBLOCK );

  if( status == -1 )
    {
      fprintf( stderr, "Error setting non blocking on socket\n" );
      return -1;
    }
  
  status = connect( sockfd, (struct sockaddr*)&servAddr, sizeof( servAddr ) );

  if( status == -1 && errno != EINPROGRESS )
    {
      fprintf( stderr, "Server not recheable\n" );
      return -1;
    }
  
#ifdef PRINT_DEBUG
  fprintf( stderr, "p on %d\n", sockfd );
#endif

  writeTran[ sockfd ] = hi;
  writeHandlers[ sockfd ] = &handleConnect; 
  delTran[ sockfd ] = hi;
  gettimeofday( &usageStamps[ sockfd ], NULL );
}
  
int handleConnect( int fd )
{
  status = fscanf( traces, "%s", presentTracePtr->URL );

  if( status != EOF )
    {
      sprintf( presentRequest, "GET %s HTTP/1.0\r\n\r\n", presentTracePtr->URL );
      
#ifdef PRINT_DEBUG
      printf( "Client %d : %s Wanted\n", fd, presentTracePtr->URL );
#endif
      
      status = write( fd, presentRequest, strlen( presentRequest ) );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Client %d : Error sending request\n", fd );
	  return 0;
	}
    }
  else
    {
      fprintf( stderr, "Done reading the URLs\n" );
      count--;

      if( count == 1 )
	{
	  fprintf( stderr, "All of them are done reading the URLs\n" );
	  exit( 0 );
	}
    }
  
  readTran[ fd ] = hi;
  readHandlers[ fd ] = &handleDownloadRead;
  writeTran[ fd ] = NULL;
  writeHandlers[ fd ] = NULL;
  delTran[ fd ] = hi;
  gettimeofday( &usageStamps[ fd ], NULL );

  return 0;
}

int handleDownloadRead( int fd )
{
  status = read( fd, buffer, BUFF_LEN );
  gettimeofday( &usageStamps[ fd ], NULL );

  if( status == 0 )
    {
      readTran[ fd ] = NULL;
      writeTran[ fd ] = NULL;
      writeHandlers[ fd ] =  NULL;
      readHandlers[ fd ] = NULL;
      close( fd );
      connectServer();
    }

  return 0;
}

int makeFDSets()
{
  int i;
  FD_ZERO( &readFDSet );
  FD_ZERO( &writeFDSet );

  highestFd = 0;

  for( i = 0; i < FD_SETSIZE; i++ )
    {
      if( readTran[ i ] != NULL )
	{
	  FD_SET( i, &readFDSet );
	  highestFd = i;
	}

      if( writeTran[ i ] != NULL )
	{
	  FD_SET( i, &writeFDSet );
	  highestFd = i;
	}
    }

  return 0;
}

int timerHelperRead()
{
  int n = TIME_GAP;
  float timeTaken;
  struct timeval now;
  int i;

  gettimeofday( &now, NULL );

  for( i = 0; i < highestFd + 1; i++ )
    {
      if( delTran[ i ] != NULL )
	{
	  timeTaken = ((float)(now.tv_sec - usageStamps[ i ].tv_sec))*1000+((float)(now.tv_usec - usageStamps[ i ].tv_usec))/1000;
      
	  if( timeTaken > (float)TIME_GAP )
	    {
	      close( i );
	      readTran[ i ] = NULL;
	      writeTran[ i ] = NULL;
	      delTran[ i ] = NULL;
	    }
	}
    }
}


	

