#include "client.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <error.h>
#include <signal.h>
#include <errno.h>
#include "common.h"

int numCli;
static char *fileName;
static char *STATIC_SERV_ADDR;
int STATIC_SERV_PORT;
int TIME_GAP;
int presentCli;
struct timeval start_time;
in_addr_t netAddr;
int numChildDone = 0;

#define TIMERSUB(a, b, result)                                                \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)

void join_handler(int signum)
{
	numChildDone++;
	fprintf(stderr, "got %d, so far %d children done\n", signum, numChildDone);
	if (numChildDone == numCli) {
		struct timeval end_time, taken;
		gettimeofday(&end_time, NULL);
		TIMERSUB(&end_time, &start_time, &taken);
		fprintf(stderr, "%d sec %d us elapsed\n", taken.tv_sec, taken.tv_usec);
		exit(-1);
	}
}

int main( int argc, char **argv )
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
  TIME_GAP = atoi( argv[ 5 ] );

  /* set signal handler */
  if (signal(SIGCHLD, join_handler) == SIG_ERR) {
  	perror("signal");
	exit(-1);
  }

  //int get = GET;  int status;
  int i;
  int status;
  struct hostent *ent;
  if ((ent = gethostbyname(STATIC_SERV_ADDR)) == NULL) {
	  fprintf( stderr, "failed in name lookup - %s\n", STATIC_SERV_ADDR );
	  exit( -1 );
  }
  memcpy(&netAddr, ent->h_addr, sizeof(netAddr));

  gettimeofday(&start_time, NULL);
  for( i = 0; i < numCli; i++ )
    {
#ifdef PRINT_DEBUG
      printf( "Client %d : Initiating system\n", i );
#endif
      presentCli = i;

      status = createSlave( &dummyClient );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot clone\n" );
	  exit( -1 );
	}
      else
	{
#ifdef PRINT_DEBUG
	  fprintf( stderr, "New Clone %d\n", status );
#endif
	}
    }

  read( status, data, 100 );

  return 0;
}

int createSlave( slave func )
{
  int status;
  int sockVec[ 2 ];

  char *childStack = (void*)malloc( STACK_SIZE );

  status = socketpair( AF_UNIX, SOCK_STREAM, 0, sockVec );

  if( status < 0 )
    {
      fprintf( stderr, "Error creating a socket pair\n" );
      exit( -1 );
    }

  childStack += STACK_SIZE;

  status = clone( func, childStack, CLONE_VM | CLONE_THREAD | CLONE_SIGHAND | CLONE_FS | SIGCHLD, (void*)presentCli );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot clone\n" );
      exit( -1 );
    }
  else
    {
#ifdef PRINT_DEBUG
      fprintf( stderr, "New Clone %d\n", status );
#endif
    }

  return sockVec[ 0 ];
}

int dummyClient( void *dat )
{
  int cliNum = (int)dat;
  int sockfd;
  struct sockaddr_in servAddr;
  // unsigned long long time;
  int status;
  int lineNo = 0;
  int numRequests = 0;
  char presentRequest[ 1000 ];
  fd_set rset;
  char data[ 10000 ];

#ifdef PRINT_DEBUG
  printf( "Client %d : Openening the trace file\n", cliNum );
#endif

  traceData *presentTracePtr = (traceData*)malloc( sizeof( traceData ) );
  FILE *traces = fopen( fileName, "r" );

  if( traces == NULL )
    {
      fprintf( stderr, "Client %d : Unable to open the trace file\n", cliNum );
      exit(-1);
      return 0;
    }

  //status = fscanf( traces, "%llu %d %d %d %d %d %d %s %s %d %d %s %d %d %d %d", &(presentTracePtr->time), &(presentTracePtr->event), &(presentTracePtr->server), &(presentTracePtr->client), &(presentTracePtr->size), &(presentTracePtr->lmod), &(presentTracePtr->status), (presentTracePtr->method), (presentTracePtr->protocol), &(presentTracePtr->server), &(presentTracePtr->port), (presentTracePtr->type), &(presentTracePtr->flags), &(presentTracePtr->path), &(presentTracePtr->query), &(presentTracePtr->urlNum) );

  status = fscanf( traces, "%s", presentTracePtr->URL );

  while( status != EOF )
    {
      lineNo++;
      if( cliNum == lineNo % numCli )
      {
	usleep( 1000 * TIME_GAP * numCli );

	sockfd = socket( AF_INET, SOCK_STREAM, 0 );
	bzero( &servAddr, sizeof( servAddr ) );
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = netAddr;
	servAddr.sin_port = htons( STATIC_SERV_PORT );
	
	status = connect( sockfd, (struct sockaddr*)&servAddr, sizeof( servAddr ) );
	if( status == -1 )
	  {
	    fprintf( stderr, "Client %d : Proxy not recheable\n", cliNum );
	    exit(-1);
	  }
	
#ifdef PRINT_DEBUG
	printf( "Client %d : Connection on %d\n", cliNum, sockfd );
#endif

	numRequests++;
	printf( "Client %d : Trace %d\n", cliNum, numRequests );
	
	
	//usleep( (int)(presentTracePtr->time - timeInit) );
	sprintf( presentRequest, "GET %s HTTP/1.0\r\n\r\n", presentTracePtr->URL );
	
#ifdef PRINT_DEBUG
	printf( "Client %d : %s Wanted\n", cliNum, presentTracePtr->URL );
#endif
	
	status = writen( sockfd, presentRequest, strlen( presentRequest ) );

	if( status == -1 )
	  {
	    fprintf( stderr, "Client %d : Error sending request\n", cliNum );
	    close( sockfd );
	    continue;
	  }

	for(;;)
	  {
	    FD_ZERO( &rset );
	    FD_SET( sockfd, &rset );

	    select( sockfd + 1, &rset, NULL, NULL, NULL );

	    if( FD_ISSET( sockfd, &rset ) )
	      {
		status = read( sockfd, data, 10000 );
		
		if( status <= 0 )
		  {
		    close( sockfd );
		    break;
		  }
		else
		  {
#ifdef PRINT_DEBUG
		    printf( "Obtained %d\n", status );
#endif
		  }
	      }
	  }
      }

      //status = EOF;
      //presentTracePtr->urlNum++;
      //      status = fscanf( traces, "%llu %d %d %d %d %d %d %s %s %d %d %s %d %d %d %d", &(presentTracePtr->time), &(presentTracePtr->event), &(presentTracePtr->server), &(presentTracePtr->client), &(presentTracePtr->size), &(presentTracePtr->lmod), &(presentTracePtr->status), (presentTracePtr->method), (presentTracePtr->protocol), &(presentTracePtr->server), &(presentTracePtr->port), (presentTracePtr->type), &(presentTracePtr->flags), &(presentTracePtr->path), &(presentTracePtr->query), &(presentTracePtr->urlNum) );
      status = fscanf( traces, "%s", presentTracePtr->URL );
    }
    kill(0, SIGCHLD);

  return 0;  
}
  
