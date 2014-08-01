#ifndef _LOCKS_H_
#define _LOCKS_H_

#include "hashtab.h"
#include "hcfs.h"

#include <stdio.h>

typedef struct countLock
{
  int reads;
  int write;
  char *data;
} countLock;

int initLocks();
int addReadLock( char *key, int hash, int match );
int addWriteLock( char *key, int hash, int match );
int removeReadLock( char *key, int hash, int match );
int removeWriteLock( char *key, int hash, int match  );
int lockOn( int hash, int match );
int lockCount();

#endif
