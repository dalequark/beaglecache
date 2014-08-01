#include "init.h"

char *readTran[ FD_SETSIZE ];
char *writeTran[ FD_SETSIZE ];
struct timeval usageStamps[ FD_SETSIZE ];
char *connTran[ FD_SETSIZE ];
char *respTran[ FD_SETSIZE ];
int (*readHandlers[ FD_SETSIZE ])( int );
int (*writeHandlers[ FD_SETSIZE ])( int );
int (*deleteHandlers[ FD_SETSIZE ])( int, struct timeval );
int numDataHelpers;
int numDNSHelpers;
int presentMapNum;
int presentConnCount;
int presentClientCount;
int memLimit;
int cLimit;
int dLimit;
char canConnect;
elevator** diskSchedulers;
int *dataHelperReads;
int *dataHelperWrites;
int *dnsHelperReads;
int *dnsHelperWrites;
int timerHelperRead;
int timerHelperWrite;
int indexHelperRead;
int indexHelperWrite;
int logHelperRead;
int logHelperWrite;
int errorHelperRead;
int errorHelperWrite;
char *datHelperStatus;
char *dnsHelperStatus;
int port;
int loop();
int listenFd;
struct sockaddr_in servAddr;
htab *asyncTab, *rTranTab, *dnsCache, *dTranURL, *fTab, *mapTab, *dnsTranTab;
int presentDataHelper;
int presentDNSHelper;
char genBuffer[ 20000 ];
int genBufferLen;
list *delReqList, *delResList, *delConList, *newConnList, *globalLargeBufferQueue, *globalSmallBufferQueue;
char *usage = "HashCache Usage:\n\t-d : Number of data helpers (1-40) [20]\n\t-n : Number of DNS helpers (1-50) [20]\n\t-p : Port to run on (1024-65536) [33333]\n\t-v : Version\n\t-h : Usage help\n\t-V : Verbose\n\t-m : Memory limit in MB (20-128) [30]\n\t-C : Total clients in system allowed (100-2048) [250]\n\t-D : Total downloads in system allowed (100-2048) [250]\n\t-f : Total cache size in GB to be created [30]\n\t-s : Total cache size in GB to be used [30]\n\t-N : Total number of objects that can be cached\n\t-i : Total index size in MB\n\t-b : Total size for buffers\n\t-P : Path to a disk\n\t-G : Use gateway\n\t-g : Gateway port [33333]\n\t-w : Download only - No caching\n\t-r : Reuse existing cache\n\t-c : Number of disks\n\n";
int *mapSize;
char **mapData;
int cacheSize;
int indexSize;
//int cloneBufferSize;
int readBufferSize;
char **fsPaths;
char mode;
int numObjects;
const char *staticGateway;
int gatewayPort = 33333;
int noCache = 0;
int mode1 = NO_REUSE;
int numDisks;
int numDisksSeen;
char *configFile;
int subnetCount;
unsigned long *subnetMasks;
unsigned long *subnetMatches;
char *logFilePath;
#ifdef USE_EPOLL
int epfd;
struct epoll_event *epollEventsIn;
struct epoll_event *epollEventsOut;
char* epollTran[ MY_FD_SETSIZE ];
char epollDat;
#endif
struct blockHeader *tempBlockHead;
int* diskLens;
int* diskSeeks;
int* diskVersions;
int* totalBatchOffsets;
int presentBatchVersion;
list **presentResponseBatch;
list** blockLists;
list *helperWaitList;
list *globalWriteWaitList;
char batchWriteLock;
int diskBatchesReady;
struct timeval timeSinceProcess;
int totalBlocksUsed;
char versionChange;
int totalResponseCount;
int totalRequestCount;
int totalConnCount;
int totalURLCount;
int totalMapEntryCount;
int totalBuffCount;
int numCC;
int numCliInfo;
int numSerInfo;
char* indexFilePath;
int direct;

int main( int argc, char **argv )
{
  fprintf(stdout, "Welcome to HashCache!");
  int status, i;
  int **fd3, **fd4;

  char opt;
  int defaultValues = 1;
  port = 33333;
  numDNSHelpers = 10;
  numDataHelpers = 3;
  cLimit = 2048;
  dLimit = 2048;
  memLimit = 30 * 1024 * 1024 / FILE_BLOCK_SIZE;
  indexSize = 6;
  //  cloneBufferSize = 20;
  port = 33333;
  numObjects = 100000;
  cacheSize = 20;
  mode = -1;
  numDisks = 1;
  numDisksSeen = 0;

  initListCounts();
  initHashtableCounts();

  while( ( opt = getopt( argc, argv, ":p:d:n:C:D:m:f:s:N:P:i:b:g:G:c:F:rwvhV" ) ) != -1 )
    {
      if( opt == ':' )
	{
	  fprintf( stderr, "Missing argument\n" );
	  fprintf( stderr, "%s", usage );
	  exit( -1 );
	}

      if( opt == '?' )
	{
	  fprintf( stderr, "Unknown argument\n" );
	  fprintf( stderr, "%s", usage );
	  exit( -1 );
	}

      if( opt == 'v' )
	{
	  fprintf( stderr, "HashCache Version: 1.0\n" );
	  continue;
	}

      if( opt == 'h' )
	{
	  fprintf( stderr, "%s", usage );
	  return 0;
	}

      if( opt == 'p' )
	{
	  port = atoi( optarg );

	  if( port < 1024 || port > 65536 )
	    {
	      fprintf( stderr, "Bad port number\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }
	  
	  continue;
	}

      if( opt == 'd' )
	{
	  numDataHelpers = atoi( optarg );

	  if( numDataHelpers <= 0 || numDataHelpers > 40 )
	    {
	      fprintf( stderr, "Too many or too few data helpers\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  continue;
	}

      if( opt == 'n' )
	{
	  numDNSHelpers = atoi( optarg );
  
	  if( numDNSHelpers <= 0 || numDNSHelpers > 50 )
	    {
	      fprintf( stderr, "Too many or too few DNS helpers\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  continue;
	}

      if( opt == 'm' )
	{
	  memLimit = atoi( optarg );
	  
	  if( memLimit <= 20 || memLimit >= 128 )
	    {
	      fprintf( stderr, "Too much or too little memory being used\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  memLimit = memLimit * 1024 * 1024 / FILE_BLOCK_SIZE;

	  continue;
        } 

      if( opt == 'D' )
	{
	  dLimit = atoi( optarg );
	    
	  if( dLimit < 100 || dLimit > 2048 )
	    {
	      fprintf( stderr, "Too less or too many downloads allowed\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  continue;
	}

      if( opt == 'C' )
	{
	  cLimit = atoi( optarg );

	  if( cLimit < 100 || cLimit > 2048 )
	    {
	      fprintf( stderr, "Too less or too many clients allowed\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  continue;
	}

      if( opt == 'f' )
	{
	  if( mode == -1 )
	    {
	      cacheSize = atoi( optarg );
	      mode = CREATE;
	    }

	  continue;
	}
      
      if( opt == 's' )
	{
	  if( mode == -1 )
	    {
	      cacheSize = atoi( optarg );
	      mode = OPEN;
	    }

	  continue;
	}

      if( opt == 'N' )
	{
	  numObjects = atoi( optarg );

#if FULL > 0
	  indexSize = numObjects;
#endif

	  continue;
	}

      if( opt == 'P' )
	{
	  if( numDisksSeen == 0 && fsPaths == NULL )
	    {
	      fsPaths = (char**)malloc( sizeof( char* ) );
	    }

	  if( numDisks == numDisksSeen )
	    {
	      fprintf( stderr, "Configured for %d disks, but setting up %d paths", numDisks, numDisks + 1 );
	      return -1;
	    }

	  fsPaths[ numDisksSeen++ ] = optarg;
	  continue;
	}

      if( opt == 'i' )
	{
#if LRU > 0 || ZIPF > 0
	  indexSize = atoi( optarg );
#endif

	  if( indexSize < 0 )
	    {
	      fprintf( stderr, "Cannot have negative index\n" );
	      exit( -1 );
	    }
	  
	  continue;
	}

      if( opt == 'b' )
	{
	  //	  cloneBufferSize = atoi( optarg );

	  /*if( cloneBufferSize < 0 )
	    {
	      fprintf( stderr, "Cannot have negative clone buffer size\n" );
	      exit( -1 );
	      }*/

	  continue;
	}

      if( opt == 'G' )
	{
	  staticGateway = optarg;

	  continue;
	}

      if( opt == 'g' )
	{
	  gatewayPort = atoi( optarg );

	  continue;
	}

      if( opt == 'w' )
	{
	  noCache = 1;

	  continue;
	}

      if( opt == 'r' )
	{
	  mode1 = REUSE;
	  mode = REUSEV;	      

	  continue;
	}

      if( opt == 'c' )
	{
	  numDisks = atoi( optarg );
	  fsPaths = (char**)malloc( sizeof(char*) * numDisks );
	  continue;
	}

      if( opt == 'F' )
	{
	  configFile = optarg;
	  continue;
	}
    }

  if( configFile != NULL )
    {
      status = parseConfigFile( configFile );

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse config file\n" );
	  return -1;
	}
    }

  globalWriteWaitList = create();
  diskBatchesReady = FALSE;
  gettimeofday( &timeSinceProcess, NULL );
  blockLists = (list**)malloc( sizeof(list*) * ( 1 + numDisks ) );
  presentResponseBatch = (list**)malloc( sizeof(list*) * ( 1 + numDisks ) );
  totalBlocksUsed = 0;

  for( i = 0; i < ( numDisks + 1 ); i++ )
    {
      blockLists[ i ] = create();
      presentResponseBatch[ i ] = create();
    }

  initBatchSystem();
  initHOC( readBufferSize );
  diskLens = (int*)malloc( sizeof( int ) * numDisks );
  diskVersions = (int*)malloc( sizeof( int ) * numDisks );
  diskSeeks = (int*)malloc( sizeof( int ) * numDisks );
  totalBatchOffsets = (int*)malloc( sizeof( int ) * numDisks );
  batchWriteLock = FALSE;
  helperWaitList = create();
  versionChange = FALSE;

  for( i = 0; i < numDisks; i++ )
    {
      totalBatchOffsets[ i ] = -1;
    }

  int x = RLIMIT_NOFILE;
  fprintf( stderr, "rlnf = %d\n", x );
  struct rlimit rlim;

  x = getrlimit( RLIMIT_NOFILE, &rlim );
  if( x < 0 ) exit( -1 );
  fprintf( stderr, "Limits: %d %d\n", (int)rlim.rlim_cur, (int)rlim.rlim_max );
  rlim.rlim_cur = MY_FD_SETSIZE;
  x = setrlimit( RLIMIT_NOFILE, &rlim );
  if( x < 0 ) exit( -1 );
  x = getrlimit( RLIMIT_NOFILE, &rlim );
  if( x < 0 ) exit( -1 );
  fprintf( stderr, "Limits: %d %d\n", (int)rlim.rlim_cur, (int)rlim.rlim_max );

#ifdef USE_EPOLL
  epfd = epoll_create( MY_FD_SETSIZE );

  if( epfd < 0 )
    {
      fprintf( stderr, "Cannot open epoll fd %d %s\n", errno, strerror( errno ) );
      exit( -1 );
    }

  epollEventsIn = malloc( MY_FD_SETSIZE * sizeof( struct epoll_event ) );
  epollEventsOut = malloc( MY_FD_SETSIZE * sizeof( struct epoll_event ) );

  for( i = 0; i < MY_FD_SETSIZE; i++ )
    {
      epollTran[ i ] = NULL;
    }
#endif

#if WAPROX > 0
#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating queue for waprox\n" );
#endif

  initSched();
#endif

#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating HCFS with %d objects at %d disks with total size %d GB at the these locations\n", numObjects, numDisks, cacheSize );
#endif

  if( numDisksSeen != numDisks )
    {
      fprintf( stderr, "Not enough paths given for the configured number of disks\n" );
      return -1;
    }

  dnsHelperStatus = malloc( sizeof( char ) * numDNSHelpers );
  dnsHelperReads = malloc( sizeof( int ) * numDNSHelpers );
  dnsHelperWrites = malloc( sizeof( int ) * numDNSHelpers );
#if USE_DNS_CLONE == 0
  fd3 = (int**)malloc( sizeof( int* ) * numDNSHelpers );
  fd4 = (int**)malloc( sizeof( int* ) * numDNSHelpers );
#endif

#if USE_DNS_CLONE > 0
  for( i = 0; i < numDNSHelpers; i++ )
    {
      status = createSlave( &dnsHelper, &dnsHelperStatus[ i ] );

#ifdef PRINT_DEBUG
      fprintf( stderr, "Creating a dns helper\n" );
#endif

      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a clone\n" );
	  exit( -1 );
	}

      dnsHelperReads[ i ] = status;
      dnsHelperWrites[ i ] = status;
    }
#else
    for( i = 0; i < numDNSHelpers; i++ )
    {
      fd3[ i ] = (int*)malloc( 2 * sizeof( int ) );

      status = pipe( fd3[ i ] );
      
      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a pipe\n" );
	  exit( -1 );
	}

      fd4[ i ] = (int*)malloc( 2 * sizeof( int ) );

      status = pipe( fd4[ i ] );
      
#ifdef PRINT_DEBUG
      fprintf( stderr, "pipes with fds %d %d %d %d\n", fd3[i][ 0 ], fd3[i][ 1 ], fd4[i][ 0 ], fd4[i][ 1 ] );
#endif

      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a pipe\n" );
	  exit( -1 );
	}
    }
  for( i = 0; i < numDNSHelpers; i++ )
    {
      status = fork();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a DNS helper\n" );
	  exit( -1 );
	}

      if( status == 0 )
	{
	  close( fd3[i][ 1 ] );
	  close( fd4[i][ 0 ] );

	  status = dnsHelper( fd3[i][ 0 ], fd4[i][ 1 ] );

	  if( status == -1 )
	    {
	      fprintf( stderr, "Error in DNS helper\n" );
	      exit( -1 );
	    }

	  return 0;
	}

#ifdef PRINT_DEBUG
      fprintf( stderr, "Created DNS helper with handles %d %d\n", fd3[ i ][ 1 ], fd4[ i ][ 0 ] );
#endif
    }

  for( i = 0; i < numDNSHelpers; i++ )
    {
      close( fd3[i][ 0 ] );
      close( fd4[i][ 1 ] );
      dnsHelperReads[ i ] = fd4[ i ][ 0 ];
      dnsHelperWrites[ i ] = fd3[ i ][ 1 ];
    }
#endif

  status = initBuffers( readBufferSize, numDataHelpers );
  
  if( status < 0 )
    {
      fprintf( stderr, "Cannot init buffers\n" );
      return -1;
    }

  status = initHCFS( numObjects, cacheSize, fsPaths, mode, numDisks, indexSize );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot initiate HCFS\n" );
      return -1;
    }

  if( defaultValues == 1 )
    {
      fprintf( stderr, "Using values for port = %d, data helpers = %d, DNS helpers = %d, memory limit = %d blocks, client limit = %d, server limit = %d, numFds = %d, file offset = %d\n", port, numDataHelpers, numDNSHelpers, memLimit, cLimit, dLimit, FD_SETSIZE, sizeof( off_t ) );
    }

  dataHelperReads = malloc( sizeof( int ) * numDataHelpers );
  dataHelperWrites = malloc( sizeof( int ) * numDataHelpers );
  datHelperStatus = malloc( sizeof( char ) * numDataHelpers );

#ifdef PRINT_DEBUG
  fprintf( stderr, "Starting up...\n" );
  fprintf( stderr, "Block Size : %d\n", FILE_BLOCK_SIZE );
  fprintf( stderr, "Creating %d data helpers\n", numDataHelpers );
  fprintf( stderr, "Creating %d DNS helpers\n", numDNSHelpers );
  fprintf( stderr, "Creating a timer helper\n" );
#endif

  diskSchedulers = malloc( numDataHelpers * sizeof( elevator* ) );

  for( i = 0; i < numDataHelpers; i++ )
    {
      status = createSlave( &dataHelper, &datHelperStatus[ i ] );
      
#ifdef PRINT_DEBUG
      fprintf( stderr, "Creating a data helper\n" );
#endif

      if( status == -1 )
	{
	  fprintf( stderr, "Error creating a clone\n" );
	  exit( -1 );
	}

      dataHelperReads[ i ] = status;
      dataHelperWrites[ i ] = status;

      diskSchedulers[ i ] = createElevator( status, 1024 * 1024 );
    }


  status = createSlave( &timerHelper, NULL );

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creatig a timer helper\n" );
#endif

  if( status == -1 )
    {
      fprintf( stderr, "Error creating a clone\n" );
      exit( -1 );
    }

  timerHelperRead = status;
  timerHelperWrite = status;

  status = createSlave( &logHelper, NULL );

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a common log helper\n" );
#endif

  if( status == -1 )
    {
      fprintf( stderr, "Error creating a clone\n" );
      exit( -1 );
    }

  logHelperRead = status;
  logHelperWrite = status;

  status = createSlave( &errorHelper, NULL );

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a error log helper\n" );
#endif

  if( status == -1 )
    {
      fprintf( stderr, "Error creating a clone\n" );
      exit( -1 );
    }

  errorHelperRead = status;
  errorHelperWrite = status;

  status = createSlave( &indexHelper, NULL );

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a index helper\n" );
#endif

  if( status == -1 )
    {
      fprintf( stderr, "Error creating a clone\n" );
      exit( -1 );
    }

  indexHelperRead = status;
  indexHelperWrite = status;

  fprintf( stderr, "Pid: %d %d\n", getpid(), getppid() );

  listenFd = socket( AF_INET, SOCK_STREAM, 0);
  
  long arg = fcntl( listenFd, F_GETFL, NULL );

  if( arg == -1 )
    {
      fprintf( stderr, "Error getting the sock fcntl on listen socket\n" );
      exit( -1 );
    }

  arg |= O_NONBLOCK;
  status = fcntl( listenFd, F_SETFL, arg );

  if( status == -1 )
    {
      fprintf( stderr, "Error setting non blocking on listen socket\n" );
      exit( -1 );
    }
  
  int enable = 1;
  bzero( &servAddr, sizeof( servAddr ) );
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = htonl( INADDR_ANY );
  servAddr.sin_port = htons( port );
  setsockopt( listenFd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable, sizeof(int) );
  status = bind( listenFd, (struct sockaddr*)(&servAddr), sizeof( servAddr ) );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot bind\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Listening now, will accept connections on %d\n", listenFd );
#endif
  
  status = listen( listenFd, LISTENQ );
  
  if( status == -1 )
    {
      fprintf( stderr, "Not able to listen\n" );
      exit( -1 );
    }

  status = initLocks();

  if( status == -1 )
    {
      fprintf( stderr, "Not able to init locks\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a table of response transactions\n" );
#endif

  rTranTab = hcreate( 7 );
  
  if( rTranTab == NULL )
    {
      fprintf( stderr, "Error creating a table of read URLs\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a table of async keys\n" );
#endif

  asyncTab = hcreate( 10 );
  
  if( asyncTab == NULL )
    {
      fprintf( stderr, "Error creating a table of async keys\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a table of connections\n" );
#endif

  status = initConnectionBase();
  
  if( status == -1 )
    {
      fprintf( stderr, "Error creating a table of connections\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a downloads table\n" );
#endif

  dTranURL = hcreate( 7 );

  if( dTranURL == NULL )
    {
      fprintf( stderr, "Cannot create downloads table\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating DNS cache\n" );
#endif

  status = initDNSCache();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot create DNS cache\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a flowstate table\n" );
#endif

  fTab = hcreate( 7 );

  if( fTab == NULL )
    {
      fprintf( stderr, "Cannot create flowstate table\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a lazy del request list\n" );
#endif

  delReqList = create();

  if( delReqList == NULL )
    {
      fprintf( stderr, "Cannot create flowstate table\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a lazy del response list\n" );
#endif

  delResList = create();

  if( delResList == NULL )
    {
      fprintf( stderr, "Cannot create flowstate table\n" );
      exit( -1 );
    }
  
#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a lazy del connection list\n" );
#endif

  delConList = create();

  if( delConList == NULL )
    {
      fprintf( stderr, "Cannot create flowstate table\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a new conn list\n" );
#endif

  newConnList = create();

  if( newConnList == NULL )
    {
      fprintf( stderr, "Cannot create flowstate table\n" );
      exit( -1 );
    }

  globalLargeBufferQueue = create();

  if( globalLargeBufferQueue == NULL )
      exit( -1 );

  globalSmallBufferQueue = create();
      
  if( globalSmallBufferQueue == NULL )
    exit( -1 );

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a table for DNS look ups going on\n" );
#endif

  dnsTranTab = hcreate( 10 );

  if( dnsTranTab == NULL )
    {
      fprintf( stderr, "Cannot create DNS table\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating map buffers of size %d\n", memLimit );
#endif

  mapTab = hcreate( 14 );
  mapSize = (int*)malloc( sizeof( int ) * memLimit );
  mapData = (char**)malloc( sizeof( char* ) * memLimit );
  presentMapNum = 0;
  presentConnCount = 0;
  presentClientCount = 0;
  canConnect = FALSE;

  for( i = 0; i < memLimit; i++ )
    {
      mapSize[ i ] = -1;
      mapData[ i ] = (char*)-1;
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating dirty cache\n" );
#endif

  initDirtyCache();

#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating HTTP details\n" );
#endif

  status = httpInit();
  
  if( status == -1 )
    {
      fprintf( stderr, "Cannot initiate HTTP details\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating hasing system\n" );
#endif

  status = initRands( indexFilePath, mode1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot initiate rands\n" );
      exit( -1 );
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating state for %d data helpers\n", numDataHelpers );
#endif

  for( i = 0; i < numDataHelpers; i++ )
    {
      readTran[  dataHelperReads[ i ] ] = malloc( 1 );
      readHandlers[ dataHelperReads[ i ] ] = &handleDataHelperRead;
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Initiating state for %d DNS helpers\n", numDNSHelpers );
#endif
  for( i = 0; i < numDNSHelpers; i++ )
    {
      readTran[  dnsHelperReads[ i ] ] = malloc( 1 );
      readHandlers[ dnsHelperReads[ i ] ] = &handleDNSHelperRead;
    }

  readTran[ timerHelperRead ] = malloc( 1 );
  readHandlers[ timerHelperRead ] = &handleTimerHelperRead;
  readTran[ indexHelperRead ] = malloc( 1 );
  readHandlers[ indexHelperRead ] = NULL;

  readTran[  listenFd ] = malloc( 1 );
  readHandlers[ listenFd ] = &handleAccept;

  for( i = 0; i < FD_SETSIZE; i++ )
    {
      usageStamps[ i ].tv_sec = -1;
    }

  status = loop();

  if( status == -1 )
    {
      fprintf( stderr, "Exceptional exit\n" );
      exit( -1 );
    }

  return 0;
}
  
void sigPipeHandler( int signum )
{
  if( signum == SIGPIPE )
    {
      fprintf( stderr, "SIGPIPE ignored\n" );
    }
}

int createSlave( slave func, char *dat )
{
  int status;
  int sockVec[ 2 ];

  char *childStack = (void*)malloc( STACK_SIZE );
  helperDat *presentHelperDat = (helperDat*)malloc( sizeof( helperDat ) );
  presentHelperDat->helperStatus = dat;

  status = socketpair( AF_UNIX, SOCK_STREAM, 0, sockVec );

  if( status < 0 )
    {
      fprintf( stderr, "Error creating a socket pair\n" );
      exit( -1 );
    }

  childStack += STACK_SIZE;
  presentHelperDat->sockFd = sockVec[ 1 ];

  status = clone( func, childStack, CLONE_VM | CLONE_THREAD | CLONE_SIGHAND | CLONE_FS | SIGKILL, (void*)presentHelperDat );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot clone\n" );
      exit( -1 );
    }
  else
    {
      fprintf( stderr, "New Clone %d\n", status );
    }

  return sockVec[ 0 ];
}

void sigChildHandler( int signal )
{
  pid_t pid;
  int status;

  pid = waitpid( -1, &status, WNOHANG | __WALL | __WCLONE );

  fprintf( stderr, "Pid %d quit\n", pid );
}

int parseConfigFile( char *newConfigFile )
{
  int status;
  int fd;
  char configs[ CONFIG_FILE_SIZE ];
  int present = 0;
  char *pArg;
  int state = PORT;
  unsigned long allMasks[ 33 ];
  int count, argCount, i;

  allMasks[ 32 ] = ULONG_MAX;

  for( i = 31; i >= 0; i-- )
    {
      allMasks[ i ] = allMasks[ i + 1 ] >> 1;
      //printf( "%d %lu\n", i + 1, allMasks[ i + 1 ] );
    }
 
  fd = open( newConfigFile, O_RDONLY );

  if( fd == -1 )
    {
      fprintf( stderr, "Cannot open onfig file\n" );
      return -1;
    }
  
  status = read( fd, configs, CONFIG_FILE_SIZE );

  if( status < 0 )
    {
      fprintf( stderr, "Cannot read config file\n" );
      return -1;
    }

  for(;;)
    {
      //fprintf( stderr, "%d\n", state );

      if( present >= status )
	{
	  break;
	}

      if( configs[ present ] == '#' )
	{
	  while( configs[ ++present ] != '\n' );
	  while( configs[ present ] == '\n' ) configs[ present++ ] = '\0';
	  continue;
	}

      
      pArg = configs + present;

      switch( state )
	{
	case PORT:
	  port = atoi( pArg );
	  
	  if( port < 1024 || port > 65536 )
	    {
	      fprintf( stderr, "Bad port number\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  state = NUMDATHELPERS;
	  fprintf( stderr, "Port : %d\n", port );
	  break;
	case NUMDATHELPERS:
	  numDataHelpers = atoi( pArg );
      
	  if( numDataHelpers <= 0 || numDataHelpers > 40 )
	    {
	      fprintf( stderr, "Too many or too few data helpers: %d\n", numDataHelpers );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }
    
	  state = NUMDNSHELPERS;
	  fprintf( stderr, "Data Helpers: %d\n", numDataHelpers ); 
	  break;
	case NUMDNSHELPERS:
	  numDNSHelpers = atoi( pArg );
      
	  if( numDNSHelpers <= 0 || numDNSHelpers > 50 )
	    {
	      fprintf( stderr, "Too many or too few DNS helpers\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  state = NUMDOWNLOADS;
	  fprintf( stderr, "DNS Helpers: %d\n", numDNSHelpers ); 
	  break;
	case NUMDOWNLOADS:
	  dLimit = atoi( pArg );
	    
	  if( dLimit < 100 || dLimit > 65535 )
	    {
	      fprintf( stderr, "Too less or too many downloads allowed\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  state = NUMCLIENTS;
	  fprintf( stderr, "Downloads : %d\n", dLimit );
	  break;
	case NUMCLIENTS:
	  cLimit = atoi( pArg );

	  if( cLimit < 100 || cLimit > 65535 )
	    {
	      fprintf( stderr, "Too less or too many clients allowed\n" );
	      fprintf( stderr, "%s", usage );
	      exit( -1 );
	    }

	  state = FORMATOPTION;
	  fprintf( stderr, "Clients : %d\n", cLimit );
	  break;
	case FORMATOPTION:
	  if( mode == -1 )
	    {
	      if( pArg[ 0 ] == 'Y' )
		mode = CREATE;
	      else
		mode = OPEN;
	    }

	  state = TOTALSIZE;
	  fprintf( stderr, "Formatting : %s\n", pArg[0] == 'Y'?"Yes":"No" );
	  break;
	case TOTALSIZE:
	  if( mode == CREATE || mode == OPEN )
	    {
	      cacheSize = atoi( pArg );
	    }

	  state = NUMOBJECTS;
	  fprintf( stderr, "Total Cache : %d\n", cacheSize );
	  break;
	case NUMOBJECTS:
	  numObjects = atoi( pArg );

#if FULL > 0
	  indexSize = numObjects;
#endif

	  state = NUMDISKS;
	  fprintf( stderr, "Number of objects : %d\n", numObjects ); 
	  break;
	case NUMDISKS:
	  numDisks = atoi( pArg );
	  numDisksSeen = 0;
	  fsPaths = (char**)malloc( sizeof(char*) * numDisks );
	  state = ALLPATHS;
	  fprintf( stderr, "Number of disks : %d\n", numDisks );
	  break;
	case ALLPATHS:
	  if( numDisksSeen == 0 && fsPaths == NULL )
	    {
	      fsPaths = (char**)malloc( sizeof( char* ) );
	    }

	  if( numDisks == numDisksSeen )
	    {
	      fprintf( stderr, "Configured for %d disks, but setting up %d paths\n", numDisks, numDisks + 1 );
	      return -1;
	    }

	  fsPaths[ numDisksSeen++ ] = pArg;
	  
	  if( numDisks == numDisksSeen )
	    {
	      state = READBUFFERSIZE;
	      break;
	    }

	  break;
	  /*	case INDEXSIZE:
#if LRU > 0 || ZIPF > 0
	  indexSize = atoi( pArg );
#endif

	  if( indexSize < 0 )
	    {
	      fprintf( stderr, "Cannot have negative index\n" );
	      exit( -1 );
	    }

	  state = READBUFFERSIZE;
	  fprintf( stderr, "Index size : %d\n", indexSize );
	  break;*/
	case READBUFFERSIZE:
	  readBufferSize = atoi( pArg );
	  
	  if( readBufferSize < 0 )
	    {
	      fprintf( stderr, "Cannot have negative read buffer size\n" );
	      exit( -1 );
	    }

	  state = GATEWAY;
	  fprintf( stderr, "Read Buffer size : %d\n", readBufferSize );
	  break;
	  /*case CLONEBUFFERSIZE:
	  cloneBufferSize = atoi( pArg );

	  if( cloneBufferSize < 0 )
	    {
	      fprintf( stderr, "Cannot have negative clone buffer size\n" );
	      exit( -1 );
	    }

	  state = GATEWAY;
	  fprintf( stderr, "Write buffer size : %d\n", cloneBufferSize );
	  break;*/
	case GATEWAY:
	  staticGateway = pArg;

	  state = GATEWAYPORT;
	  break;
	case GATEWAYPORT:
	  gatewayPort = atoi( pArg );
	  
	  if( gatewayPort == -1 )
	    {
	      staticGateway = NULL;
	    }

	  state = NOCACHEARG;
	  break;
	case NOCACHEARG:
	  if( pArg[ 0 ] == 'Y' )
	    noCache = 1;

	  state = REUSEARG;
	  break;
	case REUSEARG:
	  if( pArg[ 0 ] == 'Y' )
	    {
	      mode1 = REUSE;
	      mode = REUSEV;	      
	    }
	  state = SUBNETS;
	  break;
	case SUBNETS:
	  if( pArg[ 0 ] != '-' )
	    {
	      count = 0;
	      argCount = 0;
	      while( pArg[ count ] != '\n' || ( pArg[ count + 1 ] != '\n' && pArg[ count + 1 ] != '#' && pArg[ count + 1 ] !=  EOF ) )
		{
		  if( pArg[ count ] == '\n' )
		    {
		      pArg[ count ] = '\0';
		      argCount++;
		    }

		  if( pArg[ count ] == '/' )
		    {
		      pArg[ count ] = '\0';
		    }

		  count++;
		}

	      argCount++;
	      subnetMatches = (unsigned long*)malloc( sizeof( int ) * argCount );
	      subnetMasks = (unsigned long*)malloc( sizeof( int ) * argCount );
	      subnetCount = argCount;

	      count = 0;
	      for( i = 0; i < argCount; i++ )
		{
		  inet_aton( &pArg[ count ], (struct in_addr*)(&subnetMatches[ i ]) );

#ifdef PRINT_DEBUG
		  fprintf( stderr, "Subnet Allow : %s %lu ", &pArg[ count ], subnetMatches[ i ] );
#endif
		  
		  count += strlen( &pArg[ count ] ) + 1;
		  subnetMasks[ i ] = allMasks[ atoi( &pArg[ count ] ) ];

#ifdef PRINT_DEBUG
		  fprintf( stderr, "%lu\n", subnetMasks[ i ] );
#endif

		  count += strlen( &pArg[ count ] ) + 1;
		}	      
	    }

	  state = LOGFILEPATH;
	  break;
	case LOGFILEPATH:
	  logFilePath = pArg;
	  indexFilePath = pArg;

	  state = DIRECTDISK;
	  break;
	case DIRECTDISK:
	  if( pArg[ 0 ] == 'Y' )
	    {
	      fprintf( stderr, "Using disk devices directly\n" );
	      direct = TRUE;
	    }
	  else
	    {
	      fprintf( stderr, "Using large files\n" );
	      direct = FALSE;
	    }
	  state = CONFIGEND;
	  break;
	default:
	  break;
	}

      while( present < status && configs[ ++present ] != '\n' );

      if( present >= status && state == NONE )
	return 0;
      else
	if( present >= status )
	  return -1;

      for(;;)
	{
	  if( configs[ present ] == '\n' )
	    {
	      configs[ present ] = '\0';
	      present++;
	    }
	  else
	    {
	      break;
	    }
	}
      
      if( state == CONFIGEND )
	return 0;
    }

  return 0;
}


      
  

   
