#ifndef _BUFFER_H_
#define _BUFFER_H_

#include "list.h"
#include "hcfs.h"
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LARGE_BUFFER_SIZE WAYS * FILE_BLOCK_SIZE
#define SMALL_BUFFER_SIZE FILE_BLOCK_SIZE
#define READ_BUFFER_SIZE 256 * 1024 / FILE_BLOCK_SIZE
#define CRITICAL_MASS_BUFFER_SIZE 10 * 1024 * 1024 / FILE_BLOCK_SIZE

int initBuffers( int bufferSize, int numHelpers );
char *getLargeBuffer();
char *getSmallBuffer();
char *getSmallResponseBuffer();
char *getLargeResponseBuffer();
int returnLargeBuffer( char *buffer );
int returnSmallBuffer( char *buffer );
int returnLargeWriteBuffer( char *buffer );
int returnSmallWriteBuffer( char *buffer );
int printBufferStats();
int checkLimits( char *buffer );
int expandWriteBuffers();
int getUsedBufferCount();

#endif
