#ifndef _CLIENT_H_
#define _CLIENT_H_

typedef struct traceData {
  char URL[ 1000 ];
} traceData;


#define STACK_SIZE 131072

typedef int (*slave)(void*);

int createSlave( slave func );
int dummyClient( void *dat );

#endif
