#include "memSys.h"

#define MALLOC_CHUNK 1024

typedef struct memSysChunk{
  struct memSysChunk* next;
} memSysChunk;

typedef struct memSysMallocChunk{
  struct memSysMallocChunk* next;
} memSysMallocChunk;

struct memSys {
  memSysMallocChunk* mallocChunks;
  memSysChunk* memSysChunks;
  unsigned chunkSize;
  unsigned step;
  int numExpands;
  int total;
  int numAvail;
};

static int expand( memSys *thisMemSys );

memSys* createMemSys( int chunkSize, int step )
{
  memSys *newMemSys = malloc( sizeof( memSys ) );
  newMemSys->mallocChunks = NULL;
  newMemSys->memSysChunks = NULL;
  newMemSys->chunkSize = chunkSize;
  newMemSys->step = step;
  newMemSys->numExpands = 0;
  newMemSys->numAvail = 0;
  newMemSys->total = 0;
  expand( newMemSys );

  return newMemSys;
}

void* getChunk( memSys *thisMemSys )
{
  memSysChunk *tMemSysChunk;
  tMemSysChunk = thisMemSys->memSysChunks;
  
  if( tMemSysChunk == NULL )
    {
      expand( thisMemSys );
      tMemSysChunk = thisMemSys->memSysChunks;
    }

  thisMemSys->memSysChunks = thisMemSys->memSysChunks->next;
  thisMemSys->numAvail--;

  return (char*)(((unsigned)((char*)tMemSysChunk)) + sizeof( memSysChunk ));
}

int delChunk( memSys *thisMemSys, void *chunk )
{
  memSysChunk *t1MemChunk, *t2MemChunk;

  t1MemChunk = (memSysChunk*)( (unsigned)chunk - sizeof( memSysChunk ) );
  t2MemChunk = thisMemSys->memSysChunks;
  thisMemSys->memSysChunks = t1MemChunk;
  t1MemChunk->next = t2MemChunk;
  thisMemSys->numAvail++;
  return 0;
}

static int expand( memSys *thisMemSys )
{
  unsigned totalSize = thisMemSys->step * MALLOC_CHUNK;
  unsigned used = 0;
  memSysChunk *tMemChunk;
  memSysMallocChunk *tMallocChunk;
  thisMemSys->numExpands++;
  char *chunk = malloc( totalSize );

  tMallocChunk = thisMemSys->mallocChunks;
  thisMemSys->mallocChunks = (memSysMallocChunk*)chunk;
  thisMemSys->mallocChunks->next = tMallocChunk;

  int totalChunkSize = sizeof( memSysChunk ) + thisMemSys->chunkSize;
  used += sizeof( memSysMallocChunk );

  while( used < totalSize )
    {
      tMemChunk = (memSysChunk*)(chunk + used);
      used += totalChunkSize;
      tMemChunk->next = thisMemSys->memSysChunks;
      thisMemSys->memSysChunks = tMemChunk;
      thisMemSys->total++;
      thisMemSys->numAvail++;
    }

  return 0;
}

int deleteMemSys( memSys* thisMemSys )
{
  memSysMallocChunk *tMallocChunk = thisMemSys->mallocChunks;

  while( tMallocChunk != NULL )
    {
      free( tMallocChunk );
      tMallocChunk = tMallocChunk->next;
    }

  free( thisMemSys );

  return 0;
}

int printMemSysStats( memSys* thisMemSys )
{
  fprintf( stderr, "Number of Expands: %d\nNumber of Chunks Available: %d\nTotal Chunks: %d\n", thisMemSys->numExpands, thisMemSys->numAvail, thisMemSys->total );
  return 0;
}

