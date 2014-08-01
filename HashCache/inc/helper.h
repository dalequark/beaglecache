#ifndef _HELPER_H_
#define _HELPER_H_

#include "hcfs.h"
#include "index.h"
#include "list.h"

extern char *logFilePath;
extern char *indexFilePath;
//#define PRINT_HELPER_DEBUG 100

#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>

//extern int h_errno;
typedef struct timeval keyvalue;

typedef struct dataRep
{
  keyvalue key;
  char type;
  char *buffer;
  char *entry;
  char isBatch;
  int remOffset;
  int remLen;
  int version;
} dataRep;

typedef struct DNSRep
{
  keyvalue key;
  char presentHost[ 64 ];
  int status;
  char *entry;
} DNSRep;

#define READ 'r'
#define WRITE 'w'
#define READ_MERGE 'm'

typedef struct diskRequest
{
  keyvalue key;
  int hash;
  int block;
  char *buffer;
  char type;
  int match;
  int remOffset;
  int version;
  int writeLen;
  char *entry;
  char isBatch;
} diskRequest;

typedef struct DNSRequest
{
  char server[ 100 ];
  keyvalue key;
  list *connList;
  char *entry;
} DNSRequest;

typedef struct helperDat
{
  char *helperStatus;
  int sockFd;
} helperDat;

#if USE_DNS_CLONE > 0
int dnsHelper( void *dat );
#else
int dnsHelper( int inFd, int outFd );
#endif

int dataHelper( void *dat );
int timerHelper( void *dat );
int logHelper( void *dat );
int errorHelper( void *dat );
int indexHelper( void *dat );

int sendDataReply( int outFd, keyvalue *key, char type, char *buffer, char *entry, char isBatch, int remOffset, int version, int remLen );
int sendDNSReply( int outFd, keyvalue *key, int status, char *host, char *entry  );
int checkLimits( char *buffer );

#endif
