#ifndef _INDEX_H_
#define _INDEX_H_

#if COMPLETE_LOG > 0 || SETMEM > 0
#include "hashtab.h"
#include "list.h"
#include "hcfs.h"
#include <limits.h>
#define FLT_MAX 3.40282347e+38F
#define DBL_MAX 1.7976931348623157E+308

typedef unsigned int indexHeader;
typedef unsigned int hbits;
typedef struct setHeader setHeader;

int initIndex( unsigned int indexSize, int mode );
int updateIndexCache( int hash, setHeader *newSetHeader );
int updateUsage( int setNum );

#ifndef COMPLETE_LOG
int updateHeaders( int hash, int match, indexHeader header );
#else
int updateHeaders( unsigned int hash, int match, unsigned int indexHeader, 
		   int remOffset, int remLen, int remVersion );
#endif

setHeader* getSetHeader( int hash );
int updateClocks( setHeader *presentSetHeader, int match );
int discardHeaders( int hash, int match );
int isValid( setHeader *header );
int hdrCmp( indexHeader hash, hbits cmp );
int cacheSet( unsigned int setNum, indexHeader *newHeaders, double *ranks );
unsigned int getClock( setHeader *presentHeader, int i );
hbits getIndexHeader( setHeader *presentHeader, int i );
#if LRU > 0
int getSetNum( setHeader *presentHeader );
#endif
int whichMin( double *ranks, int size );

#ifdef COMPLETE_LOG
inline int getFirstBlockOffset( setHeader *header, int match );
inline int getFirstBlockVersion( setHeader *header, int match );
inline int getFirstBlockLen( setHeader *header, int match );
char* getIndexAddr();
int getIndexLen();
off_t getIndexOff();
#endif
void printSetStats();
#endif
#endif
