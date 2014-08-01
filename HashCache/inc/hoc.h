#ifndef _HOC_H_
#define _HOC_H_

#include <sys/mman.h>
#include <stdio.h>
#include "hashtab.h"
#include "list.h"
#include "hcfs.h"

typedef struct hotObject hotObject;
typedef int hoKey[1];

#define READL 'r'
#define WRITEL 'w'
#define EXPAND_SIZE 1024 * 1024 / FILE_BLOCK_SIZE

int initHOC( int );
hotObject *insertBlock( char *dat, int remOffset );
hotObject *deleteLRU();
char* getBlock( int );
int doneReading( int remOffset );
int doneWriting( int remOffset );
hotObject* getOldHOB( void );
#ifdef COMPLETE_LOG
int getKBlocks( list* newBlocks, int k, int start );
#else
int getKBlocks( list* newBlocks, int k, int first, int start );
#endif
int getReadLockCount();
int getWriteLockCount();
int printHOCStats();

#endif
