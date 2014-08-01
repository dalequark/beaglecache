#ifndef _SCHED_H_
#define _SCHED_H_

#include "list.h"
#include <sys/time.h>

int initSched();
int enQueueObject( int priority, char *obj );
char* moveQueue();

#endif
