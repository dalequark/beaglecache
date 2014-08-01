#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#define _FILE_OFFSET_BITS 64 

#include <features.h>
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
#include <bits/types.h>
#undef __FD_SETSIZE
#define __FD_SETSIZE MY_FD_SETSIZE
#endif

#include "hcfs.h"
#include "batch.h"
#include "hoc.h"
#include "elevator.h"

#include <sched.h>
#include <sys/ipc.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <error.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <netdb.h>
#include <getopt.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <linux/unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#include "list.h"
#include "hashtab.h"
#include "unp.h"
#include "getLocation.h"
#include "dns.h"
#include "locks.h"
#include "http.h"
#include "conn.h"
#include "index.h"
#include "buffer.h"

#define STACK_SIZE 262144
#define CONFIG_FILE_SIZE 10240
#define PORT 1
#define NUMDATHELPERS 2
#define NUMDNSHELPERS 3
#define NUMDOWNLOADS 4
#define NUMCLIENTS 5
#define FORMATOPTION 6
#define TOTALSIZE 7
#define NUMOBJECTS 8
#define NUMDISKS 9
#define ALLPATHS 10
//#define INDEXSIZE 11
//#define CLONEBUFFERSIZE 12
#define GATEWAY 13
#define GATEWAYPORT 14
#define NOCACHEARG 15
#define REUSEARG 16
#define SUBNETS 17
#define LOGFILEPATH 18
#define CONFIGEND 19
#define READBUFFERSIZE 20
#define DIRECTDISK 21
#define INDEXFILEPATH 22

//#define PRINT_DEBUG 100
typedef int (*slave)(void*);
int createSlave( slave func, char *dat );

int handleDataHelperRead( int fd );
int handleDNSHelperRead( int fd );
int handleAccept( int listenFd );
int handleTimerHelperRead( int fd );
void sigPipeHandler( int );
void sigChildHandler( int );
int parseConfigFile( char *configFileName );

#endif
