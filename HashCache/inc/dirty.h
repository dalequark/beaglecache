#ifndef _DIRTY_H_
#define _DIRTY_H_

#include "hashtab.h"
#include "list.h"
#include "hcfs.h"
#include "hoc.h"
#include <stdio.h>
#include <string.h>

typedef struct dirtyObject dirtyObject;

int initDirtyCache();
dirtyObject* createDirtyObject( char* URL, list* allBlocks );
dirtyObject* getDirtyObject( char* URL );
char* getNextBlock( dirtyObject *thisDOB );
int deleteDirtyObject( dirtyObject *thisDOB );
int reAssignBlocks( dirtyObject* thisDOB, list* newBlocks );
int nowInHOC( dirtyObject *thisDOB );
int ifUsed( dirtyObject* thisDOB );
int getNumDirty();
int getNumBlocks( dirtyObject *thisDOB );

#endif

