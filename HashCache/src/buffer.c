#include "buffer.h"

static list *largeList;
static list *smallList;
static list *largeWriteList;
static list *smallWriteList;
static int numLarge;
static int numSmall;
static int numLargeWrite;
static int numSmallWrite;
static unsigned int left;
static unsigned int right;
extern int numDisks;
char* writeBuffer;
int writeBufferSize;
char *buffer;
static int expandCount;

int initBuffers( int bufferSize, int numHelpers )
{
  int total = 0, i;
  char *base;
  unsigned aligned;

  if( bufferSize != 0 )
    {
      total = bufferSize * 1024 * 1024 / (SMALL_BUFFER_SIZE);
      numLarge = numHelpers + 5;
      numSmall = numHelpers + 5;
      numLargeWrite = numHelpers + 5;
      writeBufferSize = numDisks * 1024 * 1024 / FILE_BLOCK_SIZE;
      numSmallWrite = total - ( ( WAYS * numLargeWrite ) + numSmall + ( numLarge * READ_BUFFER_SIZE ) + writeBufferSize ); 
    }
  else
    {
      numLarge = 10;
      numSmall = 80;
    }

  //#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "%d total %d large and %d small %d largeWrite %d smallWrite %d cont. batch buffers\n", total, numLarge, numSmall, numLargeWrite, numSmallWrite, writeBufferSize );
  //#endif

  buffer = malloc( ( total + 1 ) * FILE_BLOCK_SIZE );
  mlock( buffer, (total + 1 ) * FILE_BLOCK_SIZE );

  if( buffer == NULL )
    {
      fprintf( stderr, "No memory for buffers\n" );
      exit( -1 );
    }

  left = (unsigned)buffer;
  right = (unsigned)( buffer ) + ( ( total + 1 ) * FILE_BLOCK_SIZE);

  fprintf( stderr, "Actual ends: %u %u\n", (unsigned)buffer, (unsigned)(buffer + (total + 1)* FILE_BLOCK_SIZE));

  aligned = ( (size_t)buffer / FILE_BLOCK_SIZE ) * FILE_BLOCK_SIZE;

  while( aligned < (size_t)buffer )
    aligned += FILE_BLOCK_SIZE;

  //fprintf( stderr, "%d\n", (int)buffer );
  //fprintf( stderr, "%u\n", (size_t)buffer );
  buffer = (char*)aligned;
  //fprintf( stderr, "%d\n", (int)buffer );
  //fprintf( stderr, "%u\n", (size_t)buffer );
  fprintf( stderr, "Aligned ends: %u %u\n", (unsigned)buffer, (unsigned)(buffer + (total)* FILE_BLOCK_SIZE));

  largeList = create(); 
  smallList = create();
  largeWriteList = create();
  smallWriteList = create();

  base = buffer;
  for( i = 0; i < numLarge; i++ )
    {
      //      fprintf( stderr, "%d\n", (int)base );
      //      fprintf( stderr, "%u\n", (size_t)base );
      addToHead( largeList, 0, base );
      base += (READ_BUFFER_SIZE * FILE_BLOCK_SIZE);
    }

  for( i = 0; i < numLargeWrite; i++ )
    {
      //      fprintf( stderr, "%d\n", (int)base );
      //      fprintf( stderr, "%u\n", (size_t)base );
      addToHead( largeWriteList, 0, base );
      base += LARGE_BUFFER_SIZE;
    }

  for( i = 0; i < numSmall; i++ )
    {
      //      fprintf( stderr, "%d\n", (int)base );
      //      fprintf( stderr, "%u\n", (size_t)base );
      addToHead( smallList, 0, base );
      base += SMALL_BUFFER_SIZE;
    }
  
  for( i = 0; i < numSmallWrite; i++ )
    {
      //      fprintf( stderr, "%d\n", (int)base );
      //      fprintf( stderr, "%u\n", (size_t)base );
      addToHead( smallWriteList, 0, base );
      base += SMALL_BUFFER_SIZE;
    }
    
  writeBuffer = base;

  if( (size_t)base + (FILE_BLOCK_SIZE * writeBufferSize) > right )
    {
      exit( -1 );
    }

  return 0;
}

char* getLargeBuffer()
{
  char *temp;
  //  exit( -1 );
  if( getSize( largeList ) == 0 )
    {
      return NULL;
    }
  else
    {
      temp = getHeadData( largeList );
      deleteHead( largeList );
      return temp;
    }

  return NULL;
}

char *getLargeResponseBuffer()
{
  char *temp;
  // exit( -1 );
  if( getSize( largeWriteList ) == 0 )
    {
      return NULL;
    }
  else
    {
      temp = getHeadData( largeWriteList );
      deleteHead( largeWriteList );
      return temp;
    }
  
  return NULL;
}

char *getSmallResponseBuffer()
{
  char *temp;
  // exit( -1 );
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "%d\n", getSize( smallWriteList ) );
#endif

  if( getSize( smallWriteList ) == 0 )
    {
      expandWriteBuffers();
    }
  
  temp = getHeadData( smallWriteList );
  deleteHead( smallWriteList );
  return temp;
}

char *getSmallBuffer()
{
  char *temp;
  // exit( -1 );
  if( getSize( smallList ) == 0 )
    {
      return NULL;
    }
  else
    {
      temp = getHeadData( smallList );
      deleteHead( smallList );
      return temp;
    }

  return NULL;
}

int returnLargeBuffer( char *buffer )
{
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Returning a large buffer - %d\n", getSize( largeList ) );
#endif

  if( checkLimits( buffer ) == -1 )
    {
      fprintf( stderr, "Buffer out of bounds\n" );
      exit( -1 );
    }

  addToTail( largeList, 0, buffer );
  return 0;
}

int returnSmallBuffer( char *buffer )
{
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Returning a small buffer - %d\n", getSize( smallList ) );
#endif

  if( checkLimits( buffer ) == -1 )
    {
      fprintf( stderr, "Buffer out of bounds\n" );
      exit( -1 );
    }

  addToTail( smallList, 0, buffer );
  return 0;
}

int returnSmallWriteBuffer( char *buffer )
{
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Returning a small write buffer - %d\n", getSize( smallWriteList ) );
#endif

  if( checkLimits( buffer ) == -1 )
    {
      //fprintf( stderr, "buffer out of bound\n" );
      //exit( -1 );
    }

  addToTail( smallWriteList, 0, buffer );
  return 0;
}

int returnLargeWriteBuffer( char *buffer )
{
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Returning a large write buffer - %d\n", getSize( largeWriteList ) );
#endif

  if( checkLimits( buffer ) == -1 )
    {
      fprintf( stderr, "buffer out of bound\n" );
      exit( -1 );
    }

  addToTail( largeWriteList, 0, buffer );
  return 0;
}

int checkLimits( char *buffer )
{
  if( (unsigned)buffer >= left && (unsigned)buffer <= right )
    {
      return 0;
    }

  return -1;
}

int printBufferStats()
{
  fprintf( stderr, "Write Buffers %d Write Size %d Count %d\n", 
	   getSize( smallWriteList ), FILE_BLOCK_SIZE, expandCount );
  return 0;
}

int expandWriteBuffers()
{
  int i;
  expandCount++;

  for( i = 0; i < 1024; i++ )
    {
      addToTail( smallWriteList, 0, malloc( FILE_BLOCK_SIZE ) );
      numSmallWrite++;
    }

  return 0;
}

int getUsedBufferCount() {
  return numSmallWrite - getSize( smallWriteList );
}
