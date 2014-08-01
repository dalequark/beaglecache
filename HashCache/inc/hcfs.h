#ifndef _HC_FS_H_
#define _HC_FS_H_

#include "common.h"

#define _FILE_OFFSET_BITS 64 

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>

#include "hashtab.h"

#define PAGE_SIZE 4096
#define CYCLE_CHUNKS 16
#define AVERAGE_FILE_SIZE 4096

#define CREATE 'c'
#define OPEN 'o'
#define REUSEV 'r'

typedef struct URLBlock
{
  int block;
  unsigned int hash;
  struct timeval key;
} URLBlock;

typedef struct absLocs
{
  off_t offset;
  int objFd;
  size_t mapsize;
  int remOffset;
} absLocs;

typedef struct blockHeader
{
  char URL[ URL_SIZE ];
  struct timeval key;
  unsigned int hash;
} blockHeader;

#define END 'e'
#define NOT_END 'n'

typedef struct blockTailer
{
  char status;
  int readable;
  int version;
  int pres;
  int next;
  int block;
  int length;
} blockTailer;

#define ACTUAL_BLOCK_SIZE (FILE_BLOCK_SIZE - sizeof( blockHeader ) - sizeof( blockTailer ))
#define HTTP_ACT_BLOCK_SIZE ((HTTP_BLOCK_SIZE / FILE_BLOCK_SIZE) * (FILE_BLOCK_SIZE - sizeof( blockHeader ) - sizeof( blockTailer )))
int convertRelToAbsRead( int location, unsigned int hash, int match,
			 int offset, int writeLen, absLocs *presentAbsPtr );
int convertRelToAbsWrite( unsigned int hash, int match, int block, 
			  int offset, int writeLen, absLocs *presentAbs );
int initHCFS( int num, int size, char **paths, int mode, int numDisks, 
	      int indexSize ); //, int numFirsts, int direct );
int getRemVersion();
int checkVersion( int offset, int version );
inline unsigned int getSetFromHash( unsigned int hash );
inline int getOffsetFromHash( unsigned int hash, int match );
inline int whichFirst( int offset );
inline int whichRem( int remOffset );
inline int getFromOneDisk( int num, int* start, int* version, 
			   int* disk, int* leftOnDisk );
int beginAllocCycle();
int getRemOffsetDummy();
int getRemOffset( int i );
void setLogFileLength( int );
char checkLogFileLength( int );
void printLogFileLengths();

#endif
