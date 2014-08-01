#ifndef _MEMSYS_H_
#define _MEMSYS_H_

#include <stdlib.h>
#include <stdio.h>

typedef struct memSys memSys;

memSys* createMemSys( int chunkSize, int step );
void* getChunk( memSys* myMemSys );
int delChunk( memSys* myMemSys, void* chunk );
int deleteMemSys( memSys* thisMemSys );
int printMemSysStats( memSys* thisMemSys );

#endif
